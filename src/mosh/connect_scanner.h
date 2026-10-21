// Watches ssh's terminal output during the bootstrap for mosh-server's
// "MOSH CONNECT <port> <key>" line, keeps that line from the user, and
// remembers the last line of text for when ssh fails.
//
// The output comes from a pseudo console, so it is the console host's
// rendering of what ssh wrote: text runs interleaved with cursor
// movement, cursor visibility and erase sequences, cut into chunks
// anywhere. The scanner works on the text the sequences leave visible:
// escape sequences in the middle of the line don't break it, a cursor-
// forward of one column counts as a space, and a line break or a jump to
// another row ends it.

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace casement::mosh {

struct MoshConnect {
    std::string port;  // decimal, 1..65535
    std::string key;   // 22 characters of base64
};

class ConnectScanner {
public:
    /// Takes the next chunk of output and returns what can be shown now.
    /// Text that may be the start of the line is held back until it is
    /// clear whether it is.
    std::string feed(std::string_view chunk);

    /// Returns what is held back, as when no more output came for a while.
    /// A line complete but for its end counts as found. With `end` (the
    /// output has ended) an unfinished escape sequence is returned too.
    std::string flush(bool end = false);

    /// Whether text is held back (so the caller should flush() later).
    bool holding() const { return !held_.empty(); }

    /// The first MOSH CONNECT line seen.
    const std::optional<MoshConnect>& connect() const { return connect_; }

    /// The last line of visible text that wasn't blank (trimmed), for
    /// telling the user why ssh failed.
    std::string last_line() const;

private:
    enum class Kind { text, space, line_break, other, title };
    struct Token {
        Kind kind;
        std::string bytes;
        char c = 0;  // for text: the byte; 0 for non-ASCII
        int spaces = 0;
        bool hidden = false;
    };

    void scan(Token token, std::string& out);
    void abandon(std::string& out);
    void complete(std::string& out);
    void emit(const Token& token, std::string& out);
    void end_line();

    // Splitting the byte stream into tokens.
    std::string partial_;  // an unfinished escape sequence
    // Matching.
    enum class Stage { idle, literal, port, key };
    Stage stage_ = Stage::idle;
    std::size_t matched_ = 0;  // characters of the literal, digits or key
    std::string port_, key_;
    std::vector<Token> held_;
    std::optional<MoshConnect> connect_;
    // The visible text.
    std::string line_, last_line_;
};

}  // namespace casement::mosh
