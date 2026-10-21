// The plugin end to end: casement-sim plays Casement (protocol 2, terminal
// view), fake-ssh plays ssh.exe, and tests/e2e/fake-sshd.py runs the remote
// side: a real mosh-server (or the real ssh client and sshd as well). ci/e2e.sh
// starts fake-sshd.py and sets CASEMENT_MOSH_E2E_HELPER; without it the tests
// that need a server are skipped, unless CASEMENT_E2E=1.

#include "check.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <filesystem>
#include <regex>
#include <string>
#include <thread>

#include "sim.h"

using namespace casement::sim;
using namespace std::chrono_literals;
using std::chrono::milliseconds;

namespace {

std::string env(const char* name) {
    char buf[1024];
    DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
    return n > 0 && n < sizeof(buf) ? std::string(buf, n) : std::string();
}

std::wstring here(const wchar_t* file) {
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    return (std::filesystem::path(self).parent_path() / file).wstring();
}

std::string narrow(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n > 0 ? n - 1 : 0), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), n, nullptr, nullptr);
    return out;
}

std::string base64(std::string_view bytes) {
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t i = 0;
    for (; i + 3 <= bytes.size(); i += 3) {
        std::uint32_t v = (static_cast<std::uint8_t>(bytes[i]) << 16) | (static_cast<std::uint8_t>(bytes[i + 1]) << 8) |
                          static_cast<std::uint8_t>(bytes[i + 2]);
        for (int k = 18; k >= 0; k -= 6) out += alphabet[(v >> k) & 63];
    }
    if (std::size_t rest = bytes.size() - i; rest > 0) {
        std::uint32_t v = static_cast<std::uint8_t>(bytes[i]) << 16;
        if (rest == 2) v |= static_cast<std::uint8_t>(bytes[i + 1]) << 8;
        out += alphabet[(v >> 18) & 63];
        out += alphabet[(v >> 12) & 63];
        out += rest == 2 ? alphabet[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

std::string unbase64(std::string_view text) {
    std::string out;
    std::uint32_t acc = 0;
    int bits = 0;
    for (char c : text) {
        int v = c >= 'A' && c <= 'Z' ? c - 'A'
                : c >= 'a' && c <= 'z' ? c - 'a' + 26
                : c >= '0' && c <= '9' ? c - '0' + 52
                : c == '+' ? 62
                : c == '/' ? 63
                           : -1;
        if (v < 0) continue;
        acc = (acc << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((acc >> bits) & 0xFF);
        }
    }
    return out;
}

/// The address of fake-sshd.py, or empty: then the test is skipped, or
/// fails with CASEMENT_E2E=1.
std::string helper() {
    std::string h = env("CASEMENT_MOSH_E2E_HELPER");
    if (h.empty()) {
        REQUIRE(env("CASEMENT_E2E") != "1");  // CASEMENT_MOSH_E2E_HELPER must be set
        std::fprintf(stderr, "skipped: CASEMENT_MOSH_E2E_HELPER isn't set (run ci/e2e.sh)\n");
    }
    return h;
}

/// Asks fake-sshd.py whether a mosh-server it started still runs.
bool server_alive(int pid) {
    std::string h = env("CASEMENT_MOSH_E2E_HELPER");
    auto colon = h.rfind(':');
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(h.substr(0, colon).c_str(), h.substr(colon + 1).c_str(), &hints, &res) != 0) return true;
    SOCKET s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    bool alive = true;
    if (connect(s, res->ai_addr, static_cast<int>(res->ai_addrlen)) == 0) {
        std::string req = "{\"op\":\"alive\",\"pid\":" + std::to_string(pid) + "}\n";
        send(s, req.data(), static_cast<int>(req.size()), 0);
        char buf[256];
        int n = recv(s, buf, sizeof(buf) - 1, 0);
        if (n > 0) alive = std::string(buf, static_cast<std::size_t>(n)).find("false") == std::string::npos;
    }
    closesocket(s);
    freeaddrinfo(res);
    return alive;
}

bool have_ipv6_loopback() {
    SOCKET s = socket(AF_INET6, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET) return false;
    sockaddr_in6 a{};
    a.sin6_family = AF_INET6;
    a.sin6_addr = in6addr_loopback;
    bool ok = bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0;
    closesocket(s);
    return ok;
}

/// A terminal tab: the plugin with fake-ssh as its ssh, and what it shows.
class Tab {
public:
    explicit Tab(std::vector<std::pair<std::string, std::string>> extra_env = {}) {
        std::vector<std::pair<std::string, std::string>> e{{"CASEMENT_MOSH_SSH", narrow(here(L"fake-ssh.exe"))},
                                                           {"CASEMENT_MOSH_LOG", "debug"}};
        for (auto& v : extra_env) e.push_back(std::move(v));
        c = Casement::spawn(here(L"casement-mosh.exe"), {}, true, e);
    }

    /// hello (protocol 2) and connect.
    bool start(std::string_view address, int cols = 80, int rows = 24) {
        if (!c) return false;
        auto h = c->hello(2);
        if (!h || (*h)["protocol"] != 2) return false;
        c->connect(address, "", "", json{{"cols", cols}, {"rows", rows}, {"term", "xterm-256color"}});
        return true;
    }

    /// Reads until `text` shows up in the output after the last match;
    /// false on timeout or when the plugin has ended.
    bool wait_for(std::string_view text, milliseconds timeout = 15s) {
        auto end = std::chrono::steady_clock::now() + timeout;
        for (;;) {
            if (auto at = output.find(text, mark); at != std::string::npos) {
                mark = at + text.size();
                return true;
            }
            auto left = std::chrono::duration_cast<milliseconds>(end - std::chrono::steady_clock::now());
            if (left.count() <= 0 || ended) return false;
            pump(left);
        }
    }

    /// The final state (failed or closed), waiting for it.
    std::optional<json> final_state(milliseconds timeout = 15s) {
        auto end = std::chrono::steady_clock::now() + timeout;
        while (!final_ && !ended) {
            auto left = std::chrono::duration_cast<milliseconds>(end - std::chrono::steady_clock::now());
            if (left.count() <= 0) break;
            pump(left);
        }
        return final_;
    }

    /// Reads whatever arrives for `time`.
    void idle(milliseconds time) {
        auto end = std::chrono::steady_clock::now() + time;
        for (;;) {
            auto left = std::chrono::duration_cast<milliseconds>(end - std::chrono::steady_clock::now());
            if (left.count() <= 0 || ended) return;
            pump(left);
        }
    }

    void type(std::string_view bytes) { c->send(json{{"type", "term_input"}, {"data", base64(bytes)}}); }
    void resize(int cols, int rows) { c->send(json{{"type", "term_resize"}, {"cols", cols}, {"rows", rows}}); }

    /// The pid of the mosh-server the bootstrap started.
    int server_pid() const {
        std::smatch m;
        static const std::regex detached(R"(\[mosh-server detached, pid = (\d+)\])");
        return std::regex_search(output, m, detached) ? std::stoi(m[1]) : 0;
    }

    std::unique_ptr<Casement> c;
    std::string output;
    std::size_t mark = 0;
    bool connected = false;            // state connected seen
    bool output_before_connected = false;
    bool ended = false;

private:
    void pump(milliseconds timeout) {
        auto m = c->recv(std::min(timeout, milliseconds(200)));
        if (!m) {
            if (c->wait(0ms)) ended = true;
            return;
        }
        std::string type = m->value("type", "");
        if (type == "term_output") {
            if (!connected) output_before_connected = true;
            output += unbase64(m->value("data", ""));
        } else if (type == "state") {
            std::string s = m->value("state", "");
            if (s == "connected") connected = true;
            else final_ = *m;
        }
    }

    std::optional<json> final_;
};

const char* const udp = "udp=61000:61099";

std::string address(const std::string& host = "127.0.0.1", const std::string& query = udp) {
    std::string h = env("CASEMENT_MOSH_E2E_HELPER");
    std::string port = h.substr(h.rfind(':') + 1);
    return "mosh://" + host + ":" + port + "?" + query;
}

struct Winsock {
    Winsock() {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
    }
} winsock;

}  // namespace

TEST(answers_protocol_1_with_2_and_nothing_else) {
    Tab tab;
    REQUIRE(tab.c);
    auto hello = tab.c->hello(1);
    REQUIRE(hello);
    CHECK_EQ((*hello)["protocol"].get<int>(), 2);
    CHECK(!tab.c->recv(500ms));
    tab.c->close_stdin();
    auto code = tab.c->wait(5s);
    REQUIRE(code);
    CHECK_EQ(*code, 0ul);
}

TEST(fails_on_a_bad_address) {
    Tab tab;
    REQUIRE(tab.start("mosh://host?udp=70000"));
    auto state = tab.final_state();
    REQUIRE(state);
    CHECK_EQ((*state)["state"].get<std::string>(), std::string("failed"));
    CHECK((*state)["text"].get<std::string>().find("UDP port") != std::string::npos);
}

TEST(fails_without_ssh) {
    Tab tab({{"CASEMENT_MOSH_SSH", "C:\\no\\such\\ssh.exe"}});
    REQUIRE(tab.start("mosh://127.0.0.1"));
    auto state = tab.final_state();
    REQUIRE(state);
    CHECK_EQ((*state)["state"].get<std::string>(), std::string("failed"));
    CHECK((*state)["text"].get<std::string>().find("ssh.exe") != std::string::npos);
    auto code = tab.c->wait(5s);
    REQUIRE(code);
    CHECK_EQ(*code, 1ul);
}

TEST(bootstraps_and_runs_a_shell) {
    if (helper().empty()) return;
    Tab tab;
    REQUIRE(tab.start(address()));
    // The bootstrap's output, as ssh shows it, without the MOSH CONNECT line.
    REQUIRE(tab.wait_for("mosh-server (mosh 1.4.0)"));
    REQUIRE(tab.wait_for("[mosh-server detached, pid = "));
    CHECK(tab.connected);
    CHECK(!tab.output_before_connected);
    // Then mosh: the alternate screen, and the remote shell's prompt.
    REQUIRE(tab.wait_for("\x1b[?1049h"));
    REQUIRE(tab.wait_for("e2e$ "));
    tab.type("echo hello-$((6*7))\r");
    REQUIRE(tab.wait_for("hello-42"));
    REQUIRE(tab.wait_for("e2e$ "));
    // UTF-8 both ways, wide characters and emoji included.
    tab.type("printf 'caf\\303\\251 \\344\\270\\255\\346\\226\\207 \\360\\237\\230\\200\\n'\r");
    REQUIRE(tab.wait_for("caf\xc3\xa9 \xe4\xb8\xad\xe6\x96\x87 \xf0\x9f\x98\x80"));
    tab.type("echo $TERM\r");
    REQUIRE(tab.wait_for("xterm-256color"));
    // The shell exits: mosh-server ends the session.
    tab.type("exit\r");
    auto state = tab.final_state();
    REQUIRE(state);
    CHECK_EQ((*state)["state"].get<std::string>(), std::string("closed"));
    auto code = tab.c->wait(5s);
    REQUIRE(code);
    CHECK_EQ(*code, 0ul);
    CHECK(tab.output.find("MOSH CONNECT") == std::string::npos);
    // The terminal is left as it was: alternate screen off.
    CHECK(tab.output.rfind("\x1b[?1049l") != std::string::npos);
}

TEST(follows_the_terminal_size) {
    if (helper().empty()) return;
    Tab tab;
    REQUIRE(tab.start(address(), 90, 20));
    REQUIRE(tab.wait_for("e2e$ "));
    tab.type("stty size\r");
    REQUIRE(tab.wait_for("20 90"));
    tab.resize(132, 43);
    REQUIRE(tab.wait_for("e2e$ "));
    tab.idle(500ms);
    tab.type("stty size\r");
    REQUIRE(tab.wait_for("43 132"));
    tab.c->send(json{{"type", "disconnect"}});
    tab.c->close_stdin();
    REQUIRE(tab.c->wait(5s));
}

TEST(resizes_during_the_bootstrap) {
    if (helper().empty()) return;
    // The size changes while ssh asks for a password; mosh starts with the
    // new one.
    Tab tab({{"CASEMENT_MOSH_TEST_PASSWORD", "sesame"}});
    REQUIRE(tab.start(address(), 80, 24));
    REQUIRE(tab.wait_for("password:"));
    tab.resize(100, 30);
    tab.idle(300ms);
    tab.type("sesame\r");
    REQUIRE(tab.wait_for("e2e$ "));
    tab.type("stty size\r");
    REQUIRE(tab.wait_for("30 100"));
    tab.c->send(json{{"type", "disconnect"}});
    tab.c->close_stdin();
    REQUIRE(tab.c->wait(5s));
}

TEST(answers_password_prompts_in_the_terminal) {
    if (helper().empty()) return;
    Tab tab({{"CASEMENT_MOSH_TEST_PASSWORD", "sesame"}});
    REQUIRE(tab.start(address("ann@127.0.0.1")));
    REQUIRE(tab.wait_for("ann@127.0.0.1's password:"));
    tab.type("wrong\r");
    REQUIRE(tab.wait_for("Permission denied, please try again."));
    REQUIRE(tab.wait_for("password:"));
    tab.type("sesame\r");
    REQUIRE(tab.wait_for("e2e$ "));
    CHECK(tab.output.find("sesame") == std::string::npos);  // not echoed
    tab.type("exit\r");
    auto state = tab.final_state();
    REQUIRE(state);
    CHECK_EQ((*state)["state"].get<std::string>(), std::string("closed"));
}

TEST(fails_with_ssh_s_last_line) {
    if (helper().empty()) return;
    {
        Tab tab({{"CASEMENT_MOSH_TEST_PASSWORD", "sesame"}});
        REQUIRE(tab.start(address()));
        for (int i = 0; i < 3; ++i) {
            REQUIRE(tab.wait_for("password:"));
            tab.type("wrong\r");
        }
        auto state = tab.final_state();
        REQUIRE(state);
        CHECK_EQ((*state)["state"].get<std::string>(), std::string("failed"));
        CHECK_EQ((*state)["text"].get<std::string>(), std::string("user@127.0.0.1: Permission denied (publickey,password)."));
    }
    {
        // No mosh-server on the remote host.
        Tab tab;
        REQUIRE(tab.start(address("127.0.0.1", std::string(udp) + "&server=no-such-mosh-server")));
        auto state = tab.final_state();
        REQUIRE(state);
        CHECK_EQ((*state)["state"].get<std::string>(), std::string("failed"));
        std::string text = (*state)["text"].get<std::string>();
        CHECK(text.find("no-such-mosh-server") != std::string::npos && text.find("not found") != std::string::npos);
        CHECK(tab.output.find("not found") != std::string::npos);  // shown in the terminal too
    }
}

TEST(disconnect_ends_the_server_session) {
    if (helper().empty()) return;
    Tab tab;
    REQUIRE(tab.start(address()));
    REQUIRE(tab.wait_for("e2e$ "));
    int pid = tab.server_pid();
    REQUIRE(pid > 0);
    CHECK(server_alive(pid));
    auto sent = std::chrono::steady_clock::now();
    tab.c->send(json{{"type", "disconnect"}});
    tab.c->close_stdin();
    auto code = tab.c->wait(5s);
    REQUIRE(code);
    CHECK_EQ(*code, 0ul);
    // Within Casement's two seconds.
    CHECK(std::chrono::steady_clock::now() - sent < 2s);
    // mosh-server ends too, rather than waiting for a client that roamed.
    bool gone = false;
    for (int i = 0; i < 50 && !gone; ++i) {
        gone = !server_alive(pid);
        if (!gone) std::this_thread::sleep_for(100ms);
    }
    CHECK(gone);
}

TEST(quits_with_the_escape_key) {
    if (helper().empty()) return;
    Tab tab;
    REQUIRE(tab.start(address()));
    REQUIRE(tab.wait_for("e2e$ "));
    int pid = tab.server_pid();
    tab.type("\x1e");  // Ctrl-^
    REQUIRE(tab.wait_for("Commands:"));  // mosh's help in the notification bar
    tab.type(".");
    auto state = tab.final_state();
    REQUIRE(state);
    CHECK_EQ((*state)["state"].get<std::string>(), std::string("closed"));
    bool gone = false;
    for (int i = 0; i < 50 && !gone; ++i) {
        gone = !server_alive(pid);
        if (!gone) std::this_thread::sleep_for(100ms);
    }
    CHECK(gone);
}

namespace {

struct Typing {
    bool early = false;       // the key showed before a round trip could bring it back
    bool underlined = false;  // shown underlined, as mosh marks predictions on slow links
};

/// With mosh's traffic 300 ms late each way (fake-sshd.py's relay): types a
/// key, waits for its echo, then types another and watches for 400 ms.
Typing typing_on_a_slow_link(const char* predict) {
    Typing result;
    Tab tab({{"CASEMENT_MOSH_TEST_DELAY", "300"}});
    REQUIRE(tab.start(address("127.0.0.1", std::string(udp) + "&predict=" + predict)));
    REQUIRE(tab.wait_for("e2e$ ", 20s));
    tab.idle(1500ms);  // mosh measures the round trip
    // The first key after a prompt is never shown early: mosh shows
    // predictions only once one of the same line has been confirmed.
    tab.type("e");
    REQUIRE(tab.wait_for("e", 5s));
    tab.idle(1000ms);
    std::size_t before = tab.output.size();
    tab.type("c");
    result.early = tab.wait_for("c", 400ms);
    result.underlined = tab.output.find("\x1b[0;4mc", before) != std::string::npos;
    REQUIRE(result.early || tab.wait_for("c", 5s));  // the echo itself
    tab.c->send(json{{"type", "disconnect"}});
    tab.c->close_stdin();
    tab.c->wait(5s);
    return result;
}

/// Asks fake-sshd.py's relay for mosh-server `pid` to drop everything for
/// a while.
bool pause_link(int pid, int seconds) {
    std::string h = env("CASEMENT_MOSH_E2E_HELPER");
    auto colon = h.rfind(':');
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(h.substr(0, colon).c_str(), h.substr(colon + 1).c_str(), &hints, &res) != 0) return false;
    SOCKET s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    bool paused = false;
    if (connect(s, res->ai_addr, static_cast<int>(res->ai_addrlen)) == 0) {
        std::string req = "{\"op\":\"pause\",\"pid\":" + std::to_string(pid) + ",\"seconds\":" +
                          std::to_string(seconds) + "}\n";
        send(s, req.data(), static_cast<int>(req.size()), 0);
        char buf[256];
        int n = recv(s, buf, sizeof(buf) - 1, 0);
        paused = n > 0 && std::string(buf, static_cast<std::size_t>(n)).find("true") != std::string::npos;
    }
    closesocket(s);
    freeaddrinfo(res);
    return paused;
}

}  // namespace

TEST(local_echo_modes) {
    if (helper().empty()) return;
    Typing always = typing_on_a_slow_link("always");
    CHECK(always.early);
    CHECK(always.underlined);
    Typing adaptive = typing_on_a_slow_link("adaptive");  // the link is slow, so: shown
    CHECK(adaptive.early);
    CHECK(adaptive.underlined);
    Typing never = typing_on_a_slow_link("never");
    CHECK(!never.early);
}

TEST(shows_mosh_s_notice_when_the_link_dies) {
    if (helper().empty()) return;
    Tab tab({{"CASEMENT_MOSH_TEST_DELAY", "0"}});
    REQUIRE(tab.start(address()));
    REQUIRE(tab.wait_for("e2e$ "));
    int pid = tab.server_pid();
    REQUIRE(pid > 0);
    REQUIRE(pause_link(pid, 10));
    // mosh's notification bar, through the terminal stream like the rest.
    REQUIRE(tab.wait_for("mosh: Last contact ", 12s));
    REQUIRE(tab.wait_for(" seconds ago. [To quit: Ctrl-^ .]"));
    // When the link comes back, so does the session.
    tab.type("echo back-$((40+2))\r");
    REQUIRE(tab.wait_for("back-42", 20s));
    tab.type("exit\r");
    auto state = tab.final_state();
    REQUIRE(state);
    CHECK_EQ((*state)["state"].get<std::string>(), std::string("closed"));
}

TEST(passes_ssh_options_as_typed) {
    if (helper().empty()) return;
    // ssh_args = -o "SetEnv=A=b c" -i "C:\keys dir\id" -oUser="x"
    Tab tab({{"CASEMENT_MOSH_TEST_SHOW_ARGS", "1"}});
    REQUIRE(tab.start(address("127.0.0.1", std::string(udp) +
                                               "&ssh_args=-o%20%22SetEnv%3DA%3Db%20c%22%20-i%20%22C%3A%5Ckeys%20dir"
                                               "%5Cid%22%20-oUser%3D%22x%22")));
    // fake-ssh prints the arguments it got, in order.
    for (const char* arg : {"-t", "-p", "-o", "SetEnv=A=b c", "-i", "C:\\keys dir\\id", "-oUser=x", "127.0.0.1",
                            "--", "mosh-server", "new", "-s", "-c", "256", "-l", "LANG=en_US.UTF-8", "-p",
                            "61000:61099"}) {
        CHECK(tab.wait_for(std::string("arg: ") + arg));
    }
    REQUIRE(tab.wait_for("e2e$ "));
    tab.c->send(json{{"type", "disconnect"}});
    tab.c->close_stdin();
    tab.c->wait(5s);
}

TEST(fails_when_mosh_server_never_answers) {
    if (helper().empty()) return;
    // A MOSH CONNECT line for a port nothing listens on, as when a firewall
    // drops the UDP traffic.
    Tab tab;
    REQUIRE(tab.start(address("127.0.0.1", "server=echo%20MOSH%20CONNECT%2061098%20AAAAAAAAAAAAAAAAAAAAAA%3B%20true")));
    REQUIRE(tab.wait_for("Nothing received from server on UDP port 61098.", 10s));  // mosh's notice
    auto state = tab.final_state(40s);
    REQUIRE(state);
    CHECK_EQ((*state)["state"].get<std::string>(), std::string("failed"));
    CHECK((*state)["text"].get<std::string>().find("UDP port 61098") != std::string::npos);
}

TEST(connects_over_ipv6) {
    if (helper().empty()) return;
    if (!have_ipv6_loopback()) {
        std::fprintf(stderr, "skipped: no IPv6 loopback here\n");
        return;
    }
    Tab tab;
    REQUIRE(tab.start(address("[::1]")));
    REQUIRE(tab.wait_for("e2e$ "));
    tab.type("echo $SSH_CONNECTION\r");
    REQUIRE(tab.wait_for("::1 "));
    tab.type("exit\r");
    auto state = tab.final_state();
    REQUIRE(state);
    CHECK_EQ((*state)["state"].get<std::string>(), std::string("closed"));
}

TEST(works_with_the_real_openssh) {
    if (helper().empty()) return;
    std::string user = env("CASEMENT_MOSH_E2E_OPENSSH_USER");
    if (user.empty()) {
        REQUIRE(env("CASEMENT_E2E") != "1");
        std::fprintf(stderr, "skipped: no sshd (CASEMENT_MOSH_E2E_OPENSSH_USER isn't set)\n");
        return;
    }
    // ssh and sshd are OpenSSH's: fake-ssh only relays them. The host name
    // is one only ssh's configuration knows (HostName 127.0.0.1, Port in
    // the configuration too), so the address comes from ssh -G.
    Tab tab({{"CASEMENT_MOSH_TEST_OPENSSH", "1"}, {"CASEMENT_MOSH_TEST_HELPER", env("CASEMENT_MOSH_E2E_HELPER")}});
    REQUIRE(tab.start("mosh://" + user + "@mosh-e2e?" + udp));
    REQUIRE(tab.wait_for("[mosh-server detached, pid = "));
    REQUIRE(tab.wait_for("e2e$ ", 20s));
    tab.type("echo openssh-$((6*7)) $SSH_CONNECTION\r");
    REQUIRE(tab.wait_for("openssh-42 127.0.0.1 "));
    tab.type("exit\r");
    auto state = tab.final_state();
    REQUIRE(state);
    CHECK_EQ((*state)["state"].get<std::string>(), std::string("closed"));
    CHECK(tab.output.find("MOSH CONNECT") == std::string::npos);
}
CHECK_MAIN
