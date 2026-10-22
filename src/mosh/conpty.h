// A program running in its own pseudo console (ConPTY, Windows 10 1809 and
// later): what it writes to its console comes out of output() as terminal
// output (UTF-8 text and escape sequences), and bytes written with write()
// reach it as typed input.

#pragma once

#include <windows.h>

#include <memory>
#include <string>
#include <string_view>

namespace casement::mosh {

class PseudoConsole {
public:
    /// Starts `command_line` (with `application` as the program file) in a
    /// new pseudo console of cols x rows, in `directory` if not empty.
    /// On failure returns nullptr and sets `error` to a sentence.
    static std::unique_ptr<PseudoConsole> start(const std::wstring& application, const std::wstring& command_line,
                                                const std::wstring& directory, int cols, int rows,
                                                std::string& error);
    /// Closes the console and ends the program if it still runs.
    ~PseudoConsole();
    PseudoConsole(const PseudoConsole&) = delete;
    PseudoConsole& operator=(const PseudoConsole&) = delete;

    HANDLE process() const { return process_; }
    DWORD process_id() const { return process_id_; }
    /// The read end of the console's output. Keep reading it until it
    /// breaks: the console host blocks when it isn't read.
    HANDLE output() const { return output_; }

    /// Sends bytes as input. False if the console is gone.
    bool write(std::string_view bytes);
    void resize(int cols, int rows);
    /// Closes the console: the program, if still running, is ended, the
    /// console host writes what is left and then output() breaks. Safe to
    /// call more than once.
    void close();
    /// The program's exit code, if it has exited.
    bool exit_code(DWORD& code) const;

private:
    PseudoConsole() = default;

    HPCON console_ = nullptr;
    HANDLE input_ = nullptr;
    HANDLE output_ = nullptr;
    HANDLE process_ = nullptr;
    DWORD process_id_ = 0;
};

}  // namespace casement::mosh
