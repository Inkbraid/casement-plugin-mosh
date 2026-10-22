#include "mosh/ssh.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <thread>

#include "casement/log.h"
#include "casement/text.h"

namespace casement::mosh {

namespace {

std::wstring environment(const wchar_t* name) {
    DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
    if (n == 0) return {};
    std::wstring value(n, L'\0');
    n = GetEnvironmentVariableW(name, value.data(), n);
    value.resize(n);
    return value;
}

bool is_file(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::string destination(const Options& o) { return o.user.empty() ? o.host : o.user + "@" + o.host; }

std::string trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return std::string(s);
}

}  // namespace

std::wstring find_ssh() {
    std::wstring configured = environment(L"CASEMENT_MOSH_SSH");
    if (!configured.empty()) return configured;
    std::wstring root = environment(L"SystemRoot");
    if (!root.empty()) {
        std::wstring inbox = root + L"\\System32\\OpenSSH\\ssh.exe";
        if (is_file(inbox)) return inbox;
    }
    wchar_t found[MAX_PATH];
    DWORD n = SearchPathW(nullptr, L"ssh.exe", nullptr, MAX_PATH, found, nullptr);
    if (n > 0 && n < MAX_PATH) return std::wstring(found, n);
    return {};
}

std::vector<std::string> bootstrap_arguments(const Options& o) {
    std::vector<std::string> args{"-t"};
    if (o.ssh_port) {
        args.push_back("-p");
        args.push_back(std::to_string(*o.ssh_port));
    }
    args.insert(args.end(), o.ssh_args.begin(), o.ssh_args.end());
    args.push_back(destination(o));
    // ssh joins what follows "--" with spaces into the remote command, which
    // the remote user's shell runs; `server` may hold more than a path (for
    // example "env LC_ALL=C.UTF-8 mosh-server"), as mosh's --server does.
    args.push_back("--");
    args.push_back(o.server);
    for (const char* a : {"new", "-s", "-c", "256", "-l", "LANG=en_US.UTF-8"}) args.push_back(a);
    if (!o.udp.empty()) {
        args.push_back("-p");
        args.push_back(o.udp);
    }
    return args;
}

std::vector<std::string> config_arguments(const Options& o) {
    std::vector<std::string> args{"-G"};
    if (o.ssh_port) {
        args.push_back("-p");
        args.push_back(std::to_string(*o.ssh_port));
    }
    args.insert(args.end(), o.ssh_args.begin(), o.ssh_args.end());
    args.push_back(destination(o));
    return args;
}

std::wstring command_line(const std::wstring& program, const std::vector<std::string>& args) {
    std::string line = quote_argument(narrow(program));
    for (const std::string& a : args) {
        line += ' ';
        line += quote_argument(a);
    }
    return widen(line);
}

SshConfig parse_ssh_config(std::string_view text) {
    SshConfig c;
    while (!text.empty()) {
        auto eol = text.find('\n');
        std::string line = trim(text.substr(0, eol));
        text = eol == std::string_view::npos ? std::string_view() : text.substr(eol + 1);
        auto space = line.find(' ');
        if (space == std::string::npos) continue;
        std::string key = line.substr(0, space);
        std::string value = trim(std::string_view(line).substr(space + 1));
        if (key == "hostname") c.hostname = value;
        else if (key == "port") c.port = value;
        else if (key == "addressfamily") c.address_family = value;
        else if (key == "proxyjump") c.proxy_jump = value;
        else if (key == "proxycommand") c.proxy_command = value;
    }
    return c;
}

std::optional<SshConfig> query_ssh_config(const std::wstring& ssh, const Options& options, std::string& error) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE read_end = nullptr, write_end = nullptr;
    if (!CreatePipe(&read_end, &write_end, &sa, 0)) {
        error = "can't create a pipe: " + system_message(GetLastError());
        return std::nullopt;
    }
    SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    // Only these two handles go to ssh; this process's own standard handles
    // (the protocol pipes) must not.
    HANDLE inherit[2] = {write_end, nul};
    SIZE_T list_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &list_size);
    std::vector<char> list_buffer(list_size);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(list_buffer.data());
    bool have_list = InitializeProcThreadAttributeList(attributes, 1, 0, &list_size) &&
                     UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit,
                                               nul != INVALID_HANDLE_VALUE ? sizeof(inherit) : sizeof(HANDLE),
                                               nullptr, nullptr);
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = nul != INVALID_HANDLE_VALUE ? nul : nullptr;
    si.StartupInfo.hStdOutput = write_end;
    si.StartupInfo.hStdError = write_end;
    si.lpAttributeList = have_list ? attributes : nullptr;
    std::wstring cmd = command_line(ssh, config_arguments(options));
    log::debug("running {}", narrow(cmd));
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(ssh.c_str(), cmd.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | (have_list ? EXTENDED_STARTUPINFO_PRESENT : 0),
                             nullptr, nullptr, &si.StartupInfo, &pi);
    DWORD start_error = GetLastError();
    if (have_list) DeleteProcThreadAttributeList(attributes);
    CloseHandle(write_end);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) {
        CloseHandle(read_end);
        error = "can't start ssh: " + system_message(start_error);
        return std::nullopt;
    }
    CloseHandle(pi.hThread);

    std::string text;
    std::thread reader([&] {
        char buf[4096];
        DWORD n = 0;
        while (ReadFile(read_end, buf, sizeof(buf), &n, nullptr) && n > 0) text.append(buf, n);
    });
    // ssh -G reads files and, with CanonicalizeHostname, may resolve names;
    // it doesn't connect.
    if (WaitForSingleObject(pi.hProcess, 15000) == WAIT_TIMEOUT) TerminateProcess(pi.hProcess, 1);
    reader.join();
    CloseHandle(read_end);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    if (code != 0) {
        std::string last;
        for (std::string_view rest = text; !rest.empty();) {
            auto eol = rest.find('\n');
            std::string line = trim(rest.substr(0, eol));
            if (!line.empty()) last = line;
            rest = eol == std::string_view::npos ? std::string_view() : rest.substr(eol + 1);
        }
        error = last.empty() ? "ssh -G exited with code " + std::to_string(code) : last;
        return std::nullopt;
    }
    return parse_ssh_config(text);
}

std::optional<std::string> resolve_like_ssh(const std::string& host, std::string_view address_family,
                                            std::string& error) {
    addrinfo hints{};
    hints.ai_family = address_family == "inet" ? AF_INET : address_family == "inet6" ? AF_INET6 : AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    int rc = getaddrinfo(host.c_str(), nullptr, &hints, &result);
    if (rc != 0 || !result) {
        error = "Can't find the address of " + host + ": " + system_message(static_cast<unsigned long>(rc)) + ".";
        return std::nullopt;
    }
    char text[NI_MAXHOST];
    rc = getnameinfo(result->ai_addr, static_cast<socklen_t>(result->ai_addrlen), text, sizeof(text), nullptr, 0,
                     NI_NUMERICHOST);
    freeaddrinfo(result);
    if (rc != 0) {
        error = "Can't use the address of " + host + ": " + system_message(static_cast<unsigned long>(rc)) + ".";
        return std::nullopt;
    }
    return std::string(text);
}

}  // namespace casement::mosh
