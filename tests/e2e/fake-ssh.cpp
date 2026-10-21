// fake-ssh: a stand-in for ssh.exe in the end-to-end tests (Windows OpenSSH
// doesn't run under Wine). The plugin runs it through CASEMENT_MOSH_SSH with
// the command line it would give ssh.exe. It takes ssh's options, connects
// to the destination's port, where tests/e2e/fake-sshd.py listens, and has
// that run the remote command (in a pseudo terminal, as sshd would) or hand
// everything to the real OpenSSH. It relays the output to its console, the
// plugin's pseudo console, and exits with the remote command's status.
//
// For tests it also:
//   CASEMENT_MOSH_TEST_PASSWORD=x   asks "<user>@<host>'s password: " on its
//                                   console first and fails unless x is typed
//   CASEMENT_MOSH_TEST_SHOW_ARGS=1  prints its arguments, one per line
//   CASEMENT_MOSH_TEST_OPENSSH=1    lets fake-sshd.py run the real ssh
//   CASEMENT_MOSH_TEST_HELPER=h:p   where fake-sshd.py listens, when the
//                                   destination is a name only the real
//                                   ssh's configuration knows
//   CASEMENT_MOSH_TEST_DELAY=ms     has fake-sshd.py relay mosh's UDP
//                                   traffic that much later, each way

#include <winsock2.h>
#include <ws2tcpip.h>
#include <shellapi.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

HANDLE console_out = nullptr;
HANDLE console_in = nullptr;

std::string narrow(const wchar_t* s) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n > 0 ? n - 1 : 0), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, -1, out.data(), n, nullptr, nullptr);
    return out;
}

std::string env(const char* name) {
    char buf[1024];
    DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
    return n > 0 && n < sizeof(buf) ? std::string(buf, n) : std::string();
}

void say(std::string_view text) {
    DWORD n = 0;
    WriteFile(console_out, text.data(), static_cast<DWORD>(text.size()), &n, nullptr);
}

[[noreturn]] void die(const std::string& text, int code = 255) {
    say(text + "\r\n");
    ExitProcess(static_cast<UINT>(code));
}

/// Reads a line typed on the console without echo, as ssh reads a password.
std::string read_secret() {
    DWORD mode = 0;
    GetConsoleMode(console_in, &mode);
    SetConsoleMode(console_in, (mode | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT) & ~ENABLE_ECHO_INPUT);
    std::string line;
    char c;
    DWORD n = 0;
    while (ReadFile(console_in, &c, 1, &n, nullptr) && n == 1 && c != '\n') {
        if (c != '\r') line += c;
    }
    SetConsoleMode(console_in, mode);
    say("\r\n");
    return line;
}

SOCKET connect_to(const std::string& host, const std::string& port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res)
        die("ssh: Could not resolve hostname " + host + ": No such host is known.");
    SOCKET s = INVALID_SOCKET;
    for (addrinfo* a = res; a; a = a->ai_next) {
        s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == INVALID_SOCKET) continue;
        if (connect(s, a->ai_addr, static_cast<int>(a->ai_addrlen)) == 0) break;
        closesocket(s);
        s = INVALID_SOCKET;
    }
    freeaddrinfo(res);
    if (s == INVALID_SOCKET) die("ssh: connect to host " + host + " port " + port + ": Connection refused");
    return s;
}

bool read_exact(SOCKET s, char* p, int n) {
    while (n > 0) {
        int r = recv(s, p, n, 0);
        if (r <= 0) return false;
        p += r;
        n -= r;
    }
    return true;
}

/// Sends the request and relays the answer: output frames to `out`, then
/// the exit status.
int relay(SOCKET s, const nlohmann::json& request, HANDLE out) {
    std::string line = request.dump() + "\n";
    send(s, line.data(), static_cast<int>(line.size()), 0);
    for (;;) {
        char head[5];
        if (!read_exact(s, head, 5)) die("ssh: connection closed by remote host");
        auto len = (static_cast<unsigned char>(head[1]) << 24) | (static_cast<unsigned char>(head[2]) << 16) |
                   (static_cast<unsigned char>(head[3]) << 8) | static_cast<unsigned char>(head[4]);
        if (head[0] == 'X') return static_cast<int>(len);
        std::string data(len, '\0');
        if (len > 0 && !read_exact(s, data.data(), static_cast<int>(len)))
            die("ssh: connection closed by remote host");
        DWORD n = 0;
        WriteFile(out, data.data(), static_cast<DWORD>(data.size()), &n, nullptr);
    }
}

}  // namespace

int main() {
    int argc = 0;
    wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.push_back(narrow(wargv[i]));

    // ssh's options, as OpenSSH's getopt takes them.
    const std::string_view getopt = "1246ab:c:e:fgi:kl:m:no:p:qstvxAB:CD:E:F:GI:J:KL:MNO:P:Q:R:S:TVw:W:XYy";
    std::string port = "22", user, host, address_family = "any";
    bool config = false;
    std::size_t i = 0;
    for (; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--") {
            ++i;
            break;
        }
        if (a.size() < 2 || a[0] != '-') {
            if (!host.empty()) break;  // the command starts
            auto at = a.rfind('@');
            host = at == std::string::npos ? a : a.substr(at + 1);
            if (at != std::string::npos) user = a.substr(0, at);
            continue;
        }
        for (std::size_t k = 1; k < a.size(); ++k) {
            char letter = a[k];
            auto pos = getopt.find(letter);
            bool value = pos != std::string_view::npos && pos + 1 < getopt.size() && getopt[pos + 1] == ':';
            if (letter == 'G') config = true;
            if (letter == '4') address_family = "inet";
            if (letter == '6') address_family = "inet6";
            if (!value) continue;
            std::string v = k + 1 < a.size() ? a.substr(k + 1) : (i + 1 < args.size() ? args[++i] : "");
            if (letter == 'p') port = v;
            if (letter == 'l') user = v;
            break;
        }
    }
    std::string command;
    for (; i < args.size(); ++i) command += (command.empty() ? "" : " ") + args[i];
    if (host.empty()) die("usage: ssh destination [command]", 255);

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    nlohmann::json request{{"args", args},       {"user", user},
                           {"host", host},       {"port", port},
                           {"command", command}, {"address_family", address_family},
                           {"openssh", env("CASEMENT_MOSH_TEST_OPENSSH") == "1"}};
    if (std::string delay = env("CASEMENT_MOSH_TEST_DELAY"); !delay.empty()) request["delay_ms"] = std::stoi(delay);

    std::string helper_host = host, helper_port = port;
    if (std::string helper = env("CASEMENT_MOSH_TEST_HELPER"); !helper.empty()) {
        auto colon = helper.rfind(':');
        helper_host = helper.substr(0, colon);
        helper_port = helper.substr(colon + 1);
        if (helper_host.size() > 1 && helper_host.front() == '[') helper_host = helper_host.substr(1, helper_host.size() - 2);
    }

    if (config) {
        // ssh -G: the plugin reads it from a pipe.
        request["op"] = "config";
        return relay(connect_to(helper_host, helper_port), request, GetStdHandle(STD_OUTPUT_HANDLE));
    }

    // In a pseudo console under Wine the standard handles stay empty; the
    // console is there to be opened. (Windows fills them in.)
    console_out = GetStdHandle(STD_OUTPUT_HANDLE);
    console_in = GetStdHandle(STD_INPUT_HANDLE);
    if (!console_out || console_out == INVALID_HANDLE_VALUE)
        console_out = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                  OPEN_EXISTING, 0, nullptr);
    if (!console_in || console_in == INVALID_HANDLE_VALUE)
        console_in = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                 OPEN_EXISTING, 0, nullptr);

    if (env("CASEMENT_MOSH_TEST_SHOW_ARGS") == "1") {
        for (const std::string& a : args) say("arg: " + a + "\r\n");
    }
    if (std::string password = env("CASEMENT_MOSH_TEST_PASSWORD"); !password.empty()) {
        std::string who = (user.empty() ? std::string("user") : user) + "@" + host;
        for (int attempt = 0;; ++attempt) {
            say(who + "'s password: ");
            if (read_secret() == password) break;
            if (attempt == 2) die(who + ": Permission denied (publickey,password).");
            say("Permission denied, please try again.\r\n");
        }
    }

    request["op"] = "exec";
    return relay(connect_to(helper_host, helper_port), request, console_out);
}
