#include "mosh/connect_scanner.h"

#include <algorithm>

namespace casement::mosh {

namespace {

constexpr char ESC = '\x1b';
constexpr std::string_view literal = "MOSH CONNECT ";
constexpr std::size_t key_length = 22;
// Longer escape sequences are passed on as they are.
constexpr std::size_t max_sequence = 64 * 1024;
constexpr std::size_t max_line = 4096;

bool base64_char(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/';
}

/// The length of the escape sequence at data[i] (an ESC), or 0 if it isn't
/// complete yet.
std::size_t sequence_length(std::string_view data, std::size_t i) {
    std::size_t n = data.size();
    if (i + 1 >= n) return 0;
    char kind = data[i + 1];
    if (kind == '[') {
        // CSI: parameter and intermediate bytes, then a final byte.
        for (std::size_t k = i + 2; k < n; ++k) {
            auto c = static_cast<unsigned char>(data[k]);
            if (c >= 0x40 && c <= 0x7e) return k + 1 - i;
            if (c < 0x20 || c > 0x3f) return k - i;  // malformed: ends before this byte
            if (k + 1 - i >= max_sequence) return k + 1 - i;
        }
        return 0;
    }
    if (kind == ']' || kind == 'P' || kind == 'X' || kind == '^' || kind == '_') {
        // A string, ended by ST (ESC \) or, for OSC, BEL.
        for (std::size_t k = i + 2; k < n; ++k) {
            if (data[k] == '\a' && kind == ']') return k + 1 - i;
            if (data[k] == ESC) {
                if (k + 1 >= n) return 0;
                return data[k + 1] == '\\' ? k + 2 - i : k - i;
            }
            if (k + 1 - i >= max_sequence) return k + 1 - i;
        }
        return 0;
    }
    if (std::string_view("()*+-./# %").find(kind) != std::string_view::npos) return i + 2 < n ? 3 : 0;
    return 2;
}

/// The numeric parameters of a CSI sequence ("\x1b[12;3H" -> {12, 3}).
std::vector<int> csi_params(std::string_view seq) {
    std::vector<int> params;
    int value = -1;
    for (std::size_t k = 2; k + 1 < seq.size(); ++k) {
        char c = seq[k];
        if (c >= '0' && c <= '9') {
            value = (value < 0 ? 0 : value) * 10 + (c - '0');
            if (value > 100000) value = 100000;
        } else if (c == ';') {
            params.push_back(value);
            value = -1;
        } else {
            break;  // private markers and intermediates: not used here
        }
    }
    params.push_back(value);
    return params;
}

std::string trim(std::string_view s) {
    auto blank = [](char c) { return c == ' ' || c == '\t'; };
    while (!s.empty() && blank(s.front())) s.remove_prefix(1);
    while (!s.empty() && blank(s.back())) s.remove_suffix(1);
    return std::string(s);
}

}  // namespace

std::string ConnectScanner::feed(std::string_view chunk) {
    std::string out;
    std::string data = std::move(partial_);
    partial_.clear();
    data.append(chunk);
    std::size_t i = 0;
    while (i < data.size()) {
        char c = data[i];
        if (c == ESC) {
            std::size_t len = sequence_length(data, i);
            if (len == 0) {
                partial_ = data.substr(i);
                break;
            }
            std::string_view seq(data.data() + i, len);
            Token t{Kind::other, std::string(seq)};
            char kind = len > 1 ? seq[1] : 0;
            if (kind == '[' && len > 2) {
                char final_byte = seq.back();
                bool is_private = seq[2] == '?' || seq[2] == '>' || seq[2] == '<' || seq[2] == '=';
                if (!is_private && final_byte == 'C') {
                    t.kind = Kind::space;
                    t.spaces = std::max(1, csi_params(seq).front());
                } else if (!is_private && std::string_view("HfdABEFG").find(final_byte) != std::string_view::npos) {
                    t.kind = Kind::line_break;  // the cursor goes elsewhere
                }
            } else if (kind == ']') {
                // Window and icon titles: the console host announces ssh.exe's
                // path that way, which is no title for a mosh tab.
                if (seq.size() > 3 && (seq[2] == '0' || seq[2] == '1' || seq[2] == '2') && seq[3] == ';')
                    t.kind = Kind::title;
            } else if (kind == 'D' || kind == 'E' || kind == 'M' || kind == 'c') {
                t.kind = Kind::line_break;  // IND, NEL, RI, RIS
            }
            scan(std::move(t), out);
            i += len;
            continue;
        }
        auto u = static_cast<unsigned char>(c);
        Token t;
        if (c == '\r' || c == '\n' || c == '\v' || c == '\f') {
            t.kind = Kind::line_break;
        } else if (u < 0x20 || u == 0x7f) {
            t.kind = Kind::other;
            t.c = c;  // BS and TAB matter to the visible line
        } else {
            t.kind = Kind::text;
            t.c = u < 0x80 ? c : 0;
        }
        t.bytes.assign(1, c);
        scan(std::move(t), out);
        ++i;
    }
    return out;
}

std::string ConnectScanner::flush(bool end) {
    std::string out;
    if (stage_ == Stage::key && key_.size() == key_length) complete(out);
    else abandon(out);
    if (end) {
        out += partial_;
        partial_.clear();
        end_line();
    }
    return out;
}

void ConnectScanner::scan(Token token, std::string& out) {
    if (stage_ == Stage::idle) {
        if (token.kind == Kind::text && token.c == 'M') {
            held_.push_back(std::move(token));
            stage_ = Stage::literal;
            matched_ = 1;
            port_.clear();
            key_.clear();
        } else {
            emit(token, out);
        }
        return;
    }
    // Inside a candidate line: sequences that don't move the cursor to
    // another line are part of it. Control characters (they have no c
    // unless BS or TAB) are not.
    if (token.kind == Kind::title || (token.kind == Kind::other && token.c == 0 && token.bytes[0] == ESC)) {
        held_.push_back(std::move(token));
        return;
    }
    bool single_space = (token.kind == Kind::text && token.c == ' ') || (token.kind == Kind::space && token.spaces == 1);
    switch (stage_) {
        case Stage::literal: {
            char want = literal[matched_];
            if ((want == ' ' && single_space) || (want != ' ' && token.kind == Kind::text && token.c == want)) {
                held_.push_back(std::move(token));
                if (++matched_ == literal.size()) stage_ = Stage::port;
                return;
            }
            break;
        }
        case Stage::port:
            if (token.kind == Kind::text && token.c >= '0' && token.c <= '9' && port_.size() < 5) {
                port_ += token.c;
                held_.push_back(std::move(token));
                return;
            }
            if (!port_.empty() && single_space) {
                held_.push_back(std::move(token));
                stage_ = Stage::key;
                return;
            }
            break;
        case Stage::key:
            if (token.kind == Kind::text && base64_char(token.c)) {
                if (key_.size() < key_length) {
                    key_ += token.c;
                    held_.push_back(std::move(token));
                    return;
                }
                break;  // longer than a key
            }
            if (key_.size() == key_length) {
                // Anything else ends the line's key; it isn't part of it.
                complete(out);
                scan(std::move(token), out);
                return;
            }
            break;
        case Stage::idle:
            break;
    }
    abandon(out);
    scan(std::move(token), out);
}

void ConnectScanner::abandon(std::string& out) {
    for (const Token& t : held_) emit(t, out);
    held_.clear();
    stage_ = Stage::idle;
}

void ConnectScanner::complete(std::string& out) {
    unsigned port = 0;
    for (char c : port_) port = port * 10 + static_cast<unsigned>(c - '0');
    if (port == 0 || port > 65535) {
        abandon(out);
        return;
    }
    if (!connect_) connect_ = MoshConnect{port_, key_};
    for (Token& t : held_) {
        if (t.kind == Kind::text) t.hidden = true;
        emit(t, out);
    }
    held_.clear();
    stage_ = Stage::idle;
}

void ConnectScanner::emit(const Token& token, std::string& out) {
    switch (token.kind) {
        case Kind::title:
            return;  // dropped
        case Kind::text:
            if (token.hidden) {
                // Blanked rather than removed, so that the terminal's cursor
                // stays where the console host thinks it is.
                out += ' ';
                if (line_.size() < max_line) line_ += ' ';
                return;
            }
            if (line_.size() < max_line) line_ += token.bytes;
            break;
        case Kind::space:
            if (line_.size() < max_line) line_.append(static_cast<std::size_t>(std::min(token.spaces, 256)), ' ');
            break;
        case Kind::line_break:
            end_line();
            break;
        case Kind::other:
            if (token.c == '\t') {
                if (line_.size() < max_line) line_ += ' ';
            } else if (token.c == '\b' && !line_.empty()) {
                // Back over one character, continuation bytes included.
                while (!line_.empty() && (static_cast<unsigned char>(line_.back()) & 0xC0) == 0x80) line_.pop_back();
                if (!line_.empty()) line_.pop_back();
            }
            break;
    }
    out += token.bytes;
}

void ConnectScanner::end_line() {
    std::string t = trim(line_);
    if (!t.empty()) last_line_ = std::move(t);
    line_.clear();
}

std::string ConnectScanner::last_line() const {
    std::string t = trim(line_);
    return t.empty() ? last_line_ : t;
}

}  // namespace casement::mosh
