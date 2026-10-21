#include "mosh/conpty.h"

#include <vector>

#include "casement/text.h"

namespace casement::mosh {

std::unique_ptr<PseudoConsole> PseudoConsole::start(const std::wstring& application, const std::wstring& command_line,
                                                    const std::wstring& directory, int cols, int rows,
                                                    std::string& error) {
    std::unique_ptr<PseudoConsole> pc(new PseudoConsole());
    HANDLE console_input = nullptr, console_output = nullptr;
    if (!CreatePipe(&console_input, &pc->input_, nullptr, 0) ||
        !CreatePipe(&pc->output_, &console_output, nullptr, 0)) {
        error = "Can't create a pipe for the pseudo console: " + system_message(GetLastError()) + ".";
        if (console_input) CloseHandle(console_input);
        return nullptr;
    }
    COORD size{static_cast<SHORT>(cols), static_cast<SHORT>(rows)};
    HRESULT hr = CreatePseudoConsole(size, console_input, console_output, 0, &pc->console_);
    // The console host has its own copies now.
    CloseHandle(console_input);
    CloseHandle(console_output);
    if (FAILED(hr)) {
        pc->console_ = nullptr;
        error = "Can't create a pseudo console (Windows 10 version 1809 or later is needed): " +
                system_message(static_cast<unsigned long>(hr)) + ".";
        return nullptr;
    }

    SIZE_T list_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &list_size);
    std::vector<char> list_buffer(list_size);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(list_buffer.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &list_size) ||
        !UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, pc->console_,
                                   sizeof(pc->console_), nullptr, nullptr)) {
        error = "Can't attach a program to the pseudo console: " + system_message(GetLastError()) + ".";
        return nullptr;
    }
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    // Standard handles left empty: the program gets the pseudo console's, and
    // none of this process's (its stdin is the protocol stream).
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.lpAttributeList = attributes;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = command_line;
    BOOL ok = CreateProcessW(application.c_str(), cmd.data(), nullptr, nullptr, FALSE,
                             EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT, nullptr,
                             directory.empty() ? nullptr : directory.c_str(), &si.StartupInfo, &pi);
    DWORD start_error = GetLastError();
    DeleteProcThreadAttributeList(attributes);
    if (!ok) {
        error = "Can't start " + narrow(application) + ": " + system_message(start_error) + ".";
        return nullptr;
    }
    CloseHandle(pi.hThread);
    pc->process_ = pi.hProcess;
    pc->process_id_ = pi.dwProcessId;
    return pc;
}

PseudoConsole::~PseudoConsole() {
    // Nobody reads the output any more; the console host must not wait for
    // its last frame to be read when it closes.
    if (output_) CloseHandle(output_);
    close();
    if (input_) CloseHandle(input_);
    if (process_) {
        if (WaitForSingleObject(process_, 0) == WAIT_TIMEOUT) TerminateProcess(process_, 1);
        CloseHandle(process_);
    }
}

bool PseudoConsole::write(std::string_view bytes) {
    while (!bytes.empty()) {
        DWORD written = 0;
        if (!input_ || !WriteFile(input_, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) ||
            written == 0)
            return false;
        bytes.remove_prefix(written);
    }
    return true;
}

void PseudoConsole::resize(int cols, int rows) {
    if (console_) ResizePseudoConsole(console_, COORD{static_cast<SHORT>(cols), static_cast<SHORT>(rows)});
}

void PseudoConsole::close() {
    if (console_) {
        ClosePseudoConsole(console_);
        console_ = nullptr;
    }
}

bool PseudoConsole::exit_code(DWORD& code) const {
    if (!process_ || WaitForSingleObject(process_, 0) != WAIT_OBJECT_0) return false;
    return GetExitCodeProcess(process_, &code) != 0;
}

}  // namespace casement::mosh
