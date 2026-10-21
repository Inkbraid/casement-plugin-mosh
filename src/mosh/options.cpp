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

// Options that make ssh run a program, load a library or write a file on
// this computer: -E (log file), -F (configuration file), -I (PKCS#11
// library) and these -o keywords. An address may come from someone else;
// the user's own ssh configuration is the place for them.
constexpr std::string_view ssh_local_letters = "EFI";
constexpr std::string_view ssh_local_keywords[] = {"KnownHostsCommand", "LocalCommand",        "PermitLocalCommand",
                                                   "PKCS11Provider",    "ProxyCommand",        "SecurityKeyProvider",
                                                   "XAuthLocation"};

/// An -o value split the way ssh reads it (strdelim() in OpenSSH's misc.c):
/// the keyword, in lower case as ssh compares it, and the rest. Blanks and
/// an '=' before the keyword are skipped, quotes group, a blank or an '='
/// ends it. Where this differs from ssh, it finds a keyword ssh wouldn't.
std::pair<std::string, std::string_view> split_option(std::string_view value) {
    auto blank = [](char c) { return c == ' ' || c == '\t'; };
    std::size_t i = 0;
    while (i < value.size() && (blank(value[i]) || value[i] == '=')) ++i;
    std::string keyword;
    bool quoted = false;
    for (; i < value.size(); ++i) {
        char c = value[i];
        if (c == '"') {
            quoted = !quoted;
            continue;
        }
        if (!quoted && (blank(c) || c == '=')) break;
        keyword += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    while (i < value.size() && (blank(value[i]) || value[i] == '=')) ++i;
    return {keyword, value.substr(i)};
}

// Host and user names end up in commands: ssh puts them into the commands
// of ProxyCommand, LocalCommand, KnownHostsCommand and Match exec in the
// user's configuration (%h, %r, ...), and ProxyJump's hosts into the
// command it builds for them. OpenSSH 9.6 refuses shell metacharacters in
// names given on its command line; Windows comes with an older OpenSSH, so
// the plugin refuses them as OpenSSH 9.6 does (valid_hostname() and
// valid_ruser() in ssh.c), and a leading '-' as well.

bool valid_host_name(std::string_view s) {
    if (s.empty() || s[0] == '-') return false;
    return std::none_of(s.begin(), s.end(), [](char c) {
        auto u = static_cast<unsigned char>(c);
        return u <= 0x20 || u == 0x7f || std::string_view("'`\"$\\;&<>|(){},").find(c) != std::string_view::npos;
    });
}

bool valid_user_name(std::string_view s) {
    if (s.empty() || s[0] == '-' || s.back() == '\\' || has_control_characters(s)) return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (std::string_view("'`\";&<>|(){}").find(s[i]) != std::string_view::npos) return false;
        if ((s[i] == ' ' || s[i] == '\t') && i + 1 < s.size() && s[i + 1] == '-') return false;
    }
    return true;
}

/// ProxyJump's value: "none", or hosts separated by commas, each
/// [user@]host[:port] or ssh://[user@]host[:port].
bool valid_jump_hosts(std::string_view s) {
    if (s.empty()) return false;
    for (;;) {
        auto comma = s.find(',');
        if (!valid_host_name(s.substr(0, comma))) return false;
        if (comma == std::string_view::npos) return true;
        s.remove_prefix(comma + 1);
    }
}

std::string local_only(std::string_view option) {
    return std::format(
        "The extra ssh options can't include {}: options that can make ssh run programs, load libraries or write "
        "files on this computer aren't taken from an address, which may come from someone else. Your ssh "
        "configuration file (.ssh\\config in your user folder) can set it.",
        option);
}

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
            if (ssh_local_letters.find(letter) != std::string_view::npos) {
                error = local_only(std::format("-{}", letter));
                return false;
            }
            auto pos = ssh_getopt.find(letter);
            bool takes_value = pos != std::string_view::npos && pos + 1 < ssh_getopt.size() && ssh_getopt[pos + 1] == ':' &&
                               letter != ':';
            if (!takes_value) continue;
            // The value is the rest of this argument, or the next one.
            std::string_view value = std::string_view(a).substr(k + 1);
            if (value.empty()) {
                if (i + 1 >= args.size()) {
                    error = std::format("The ssh option -{} in the extra ssh options needs a value.", letter);
                    return false;
                }
                value = args[++i];
            }
            bool valid_name = true;
            if (letter == 'l') valid_name = valid_user_name(value);
            if (letter == 'J') valid_name = valid_jump_hosts(value);
            if (letter == 'o') {
                auto [keyword, rest] = split_option(value);
                for (std::string_view local : ssh_local_keywords) {
                    if (lower(local) == keyword) {
                        error = local_only(std::format("-o {}", local));
                        return false;
                    }
                }
                if (keyword == "user") valid_name = valid_user_name(rest);
                if (keyword == "hostname") valid_name = valid_host_name(rest);
                if (keyword == "proxyjump") valid_name = valid_jump_hosts(rest);
            }
            if (!valid_name) {
                error = std::format(
                    "“{}” in the extra ssh options can't be used as a host or user name: names can't start with "
                    "'-' or contain quotes or characters such as $ ; & | < > ( ) {{ }}.",
                    value);
                return false;
            }
            break;
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
    // A leading '-' would also make ssh read them as options.
    if (!valid_host_name(o.host)) return bad(std::format("“{}” isn't a host name.", o.host));
    if (!o.user.empty() && !valid_user_name(o.user))
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
