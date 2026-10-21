#include "mosh/options.h"

#include <algorithm>
#include <cctype>
#include <format>

namespace casement::mosh {

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool has_control_characters(std::string_view s) {
    return std::any_of(s.begin(), s.end(), [](char c) {
        auto u = static_cast<unsigned char>(c);
        return u < 0x20 || u == 0x7f;
    });
}

std::string trim(std::string_view s) {
    auto blank = [](char c) { return c == ' ' || c == '\t'; };
    while (!s.empty() && blank(s.front())) s.remove_prefix(1);
    while (!s.empty() && blank(s.back())) s.remove_suffix(1);
    return std::string(s);
}

/// Parses 1..65535; nullopt otherwise.
std::optional<unsigned> parse_port(std::string_view s) {
    if (s.empty() || s.size() > 5) return std::nullopt;
    unsigned v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return std::nullopt;
        v = v * 10 + static_cast<unsigned>(c - '0');
    }
    if (v == 0 || v > 65535) return std::nullopt;
    return v;
}

// ssh's own option letters (OpenSSH's getopt string); a ':' after a letter
// means it takes a value.
constexpr std::string_view ssh_getopt = "1246ab:c:e:fgi:kl:m:no:p:qstvxAB:CD:E:F:GI:J:KL:MNO:P:Q:R:S:TVw:W:XYy";

// Options that would stop ssh from running mosh-server, or run something
// else instead of it.
constexpr std::string_view ssh_forbidden = "fsGNOQVW";

}  // namespace

std::string_view to_string(Predict predict) {
    switch (predict) {
        case Predict::adaptive: return "adaptive";
        case Predict::always: return "always";
        case Predict::never: return "never";
    }
    return "adaptive";
}

std::optional<std::vector<std::string>> split_arguments(std::string_view text) {
    // The C runtime's rules (argv_parsing.cpp in the UCRT):
    //   2N backslashes + "    -> N backslashes, and a quote starts or ends
    //   2N+1 backslashes + "  -> N backslashes and a literal "
    //   "" inside quotes      -> a literal "
    //   other backslashes     -> themselves
    std::vector<std::string> args;
    std::size_t i = 0;
    const std::size_t n = text.size();
    for (;;) {
        while (i < n && (text[i] == ' ' || text[i] == '\t')) ++i;
        if (i >= n) break;
        std::string arg;
        bool quoted = false;
        for (;;) {
            std::size_t slashes = 0;
            while (i < n && text[i] == '\\') ++i, ++slashes;
            bool copy = true;
            if (i < n && text[i] == '"') {
                if (slashes % 2 == 0) {
                    if (quoted && i + 1 < n && text[i + 1] == '"') {
                        ++i;  // the second quote of "" is copied below
                    } else {
                        copy = false;
                        quoted = !quoted;
                    }
                }
                slashes /= 2;
            }
            arg.append(slashes, '\\');
            if (i >= n || (!quoted && (text[i] == ' ' || text[i] == '\t'))) break;
            if (copy) arg += text[i];
            ++i;
        }
        if (quoted) return std::nullopt;
        args.push_back(std::move(arg));
    }
    return args;
}

std::string quote_argument(std::string_view arg) {
    if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string_view::npos) return std::string(arg);
    std::string out = "\"";
    for (std::size_t i = 0;; ++i) {
        std::size_t slashes = 0;
        while (i < arg.size() && arg[i] == '\\') ++i, ++slashes;
        if (i == arg.size()) {
            // Backslashes before the closing quote must not escape it.
            out.append(slashes * 2, '\\');
            break;
        }
        if (arg[i] == '"') {
            out.append(slashes * 2 + 1, '\\');
            out += '"';
        } else {
            out.append(slashes, '\\');
            out += arg[i];
        }
    }
    out += '"';
    return out;
}

bool check_ssh_args(const std::vector<std::string>& args, std::string& error) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--" || a.size() < 2 || a[0] != '-') {
            error = std::format(
                "“{}” in the extra ssh options isn't an ssh option. Only options go there; the host and "
                "user have their own fields.",
                a);
            return false;
        }
        for (std::size_t k = 1; k < a.size(); ++k) {
            char letter = a[k];
            if (ssh_forbidden.find(letter) != std::string_view::npos) {
                error = std::format("The extra ssh options can't include -{}: ssh has to start mosh-server.", letter);
                return false;
            }
            auto pos = ssh_getopt.find(letter);
            bool takes_value = pos != std::string_view::npos && pos + 1 < ssh_getopt.size() && ssh_getopt[pos + 1] == ':' &&
                               letter != ':';
            if (!takes_value) continue;
            if (k + 1 == a.size()) {
                // The value is the next argument.
                if (i + 1 >= args.size()) {
                    error = std::format("The ssh option -{} in the extra ssh options needs a value.", letter);
                    return false;
                }
                ++i;
            }
            break;  // the rest of this argument is the value
        }
    }
    return true;
}

bool valid_udp_ports(std::string_view text) {
    auto colon = text.find(':');
    if (colon == std::string_view::npos) return parse_port(text).has_value();
    auto low = parse_port(text.substr(0, colon));
    auto high = parse_port(text.substr(colon + 1));
    return low && high && *low <= *high;
}

std::optional<Options> parse_options(const Address& address, std::string_view username, std::string& error) {
    auto bad = [&](std::string text) -> std::optional<Options> {
        error = std::move(text);
        return std::nullopt;
    };
    if (address.scheme != "mosh") return bad(std::format("“{}://” isn't a mosh address.", address.scheme));
    if (!address.path.empty()) return bad("Mosh addresses have no path; remove the part after the host.");

    Options o;
    o.host = address.host;
    o.user = address.user.empty() ? std::string(username) : address.user;
    o.ssh_port = address.port;
    // ssh would take a leading '-' as the start of an option.
    if (o.host.empty() || o.host[0] == '-' || has_control_characters(o.host) ||
        o.host.find_first_of(" \t\"'\\") != std::string::npos)
        return bad(std::format("“{}” isn't a host name.", o.host));
    if (!o.user.empty() &&
        (o.user[0] == '-' || has_control_characters(o.user) || o.user.find_first_of("\"") != std::string::npos))
        return bad(std::format("“{}” can't be used as a user name.", o.user));

    for (const auto& [name, value] : address.query) {
        if (name == "udp") {
            std::string v = trim(value);
            if (v.empty()) continue;
            if (!valid_udp_ports(v))
                return bad(std::format(
                    "“{}” isn't a UDP port or range; use a port such as 60001 or a range such as 60000:61000.", value));
            o.udp = v;
        } else if (name == "server") {
            std::string v = trim(value);
            if (v.empty()) continue;
            if (has_control_characters(v)) return bad("The mosh-server command can't contain control characters.");
            o.server = v;
        } else if (name == "predict") {
            std::string v = lower(trim(value));
            if (v.empty() || v == "adaptive") o.predict = Predict::adaptive;
            else if (v == "always") o.predict = Predict::always;
            else if (v == "never") o.predict = Predict::never;
            else
                return bad(std::format("“{}” isn't a local echo setting; use adaptive, always or never.", value));
        } else if (name == "ssh_args") {
            if (has_control_characters(value)) return bad("The extra ssh options can't contain control characters.");
            auto args = split_arguments(value);
            if (!args) return bad("The extra ssh options have a quote that isn't closed.");
            if (!check_ssh_args(*args, error)) return std::nullopt;
            o.ssh_args = std::move(*args);
        }
        // Anything else is from a newer version or a typo; ssh doesn't need it.
    }
    return o;
}

}  // namespace casement::mosh
