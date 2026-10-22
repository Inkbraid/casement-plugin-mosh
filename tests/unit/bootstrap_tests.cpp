// The bootstrap's pieces: finding mosh-server's MOSH CONNECT line in what a
// pseudo console writes, the ssh command lines, and ssh -G's output.

#include "check.h"

#include <winsock2.h>

#include "casement/address.h"
#include "mosh/connect_scanner.h"
#include "mosh/options.h"
#include "mosh/ssh.h"

using namespace casement;
using namespace casement::mosh;

namespace {

const std::string key = "4NeCCgvZFe2RnPgrcU1PQw";
const std::string line = "MOSH CONNECT 60001 " + key;
const std::string blank(line.size(), ' ');

/// Feeds the chunks one after another and flushes at the end, as the
/// bootstrap does when ssh has exited.
std::string scan(ConnectScanner& s, const std::vector<std::string>& chunks) {
    std::string out;
    for (const std::string& c : chunks) out += s.feed(c);
    return out + s.flush(true);
}

std::string scan(const std::vector<std::string>& chunks, std::optional<MoshConnect>* found = nullptr) {
    ConnectScanner s;
    std::string out = scan(s, chunks);
    if (found) *found = s.connect();
    return out;
}

Options options_for(std::string_view address) {
    std::string error;
    auto a = parse_address(address, error);
    REQUIRE(a);
    auto o = parse_options(*a, {}, error);
    REQUIRE(o);
    return *o;
}

using Args = std::vector<std::string>;

}  // namespace

TEST(finds_and_blanks_the_line) {
    // As a terminal shows mosh-server's output: on a line of its own.
    std::optional<MoshConnect> found;
    std::string out = scan({"\r\n\r\n" + line + "\r\n[mosh-server detached, pid = 42]\r\n"}, &found);
    REQUIRE(found);
    CHECK_EQ(found->port, std::string("60001"));
    CHECK_EQ(found->key, key);
    CHECK_EQ(out, "\r\n\r\n" + blank + "\r\n[mosh-server detached, pid = 42]\r\n");
    CHECK(out.find("MOSH") == std::string::npos);
}

TEST(finds_the_line_in_console_host_output) {
    // As Wine's and Windows' console hosts render it: cursor hidden around
    // the text, spaces as cursor-forward, colours.
    std::optional<MoshConnect> found;
    std::string out = scan({"\x1b[?25l" + line + "\r\n\x1b[?25h"}, &found);
    REQUIRE(found);
    CHECK_EQ(out, "\x1b[?25l" + blank + "\r\n\x1b[?25h");

    std::string forward = "MOSH\x1b[1CCONNECT\x1b[1C60001\x1b[1C" + key + "\x1b[K\r\n";
    out = scan({forward}, &found);
    REQUIRE(found);
    CHECK_EQ(found->key, key);
    CHECK_EQ(out, "    \x1b[1C       \x1b[1C     \x1b[1C" + std::string(22, ' ') + "\x1b[K\r\n");

    out = scan({"\x1b[32mMOSH CON\x1b[0mNECT 61000 " + key + "\x1b[m\r\n"}, &found);
    REQUIRE(found);
    CHECK_EQ(found->port, std::string("61000"));
    CHECK(out.find("MOSH") == std::string::npos && out.find(key) == std::string::npos);
}

TEST(finds_the_line_across_chunk_boundaries) {
    const std::string text = "mosh-server (mosh 1.4.0)\r\n\x1b[?25l\r\n" + line + "\r\n\x1b[?25h[mosh-server detached]\r\n";
    const std::string expected = scan({text});
    // Split in two at every position, and fed byte by byte.
    for (std::size_t i = 0; i <= text.size(); ++i) {
        std::optional<MoshConnect> found;
        std::string out = scan({text.substr(0, i), text.substr(i)}, &found);
        CHECK_EQ(out, expected);
        CHECK(found && found->key == key && found->port == "60001");
    }
    std::vector<std::string> bytes;
    for (char c : text) bytes.emplace_back(1, c);
    std::optional<MoshConnect> found;
    CHECK_EQ(scan(bytes, &found), expected);
    CHECK(found.has_value());
}

TEST(holds_back_only_what_may_be_the_line) {
    ConnectScanner s;
    // A prompt goes out at once.
    CHECK_EQ(s.feed("user@host's password: "), std::string("user@host's password: "));
    CHECK(!s.holding());
    // The start of the line waits for the rest...
    CHECK_EQ(s.feed("\r\nMOSH CONN"), std::string("\r\n"));
    CHECK(s.holding());
    CHECK_EQ(s.feed("ECT 60001 " + key.substr(0, 10)), std::string());
    CHECK_EQ(s.feed(key.substr(10) + "\r\n"), blank + "\r\n");
    CHECK(!s.holding());
    // ...and text that only starts like it comes out after a pause.
    CHECK_EQ(s.feed("Updated MOS"), std::string("Updated "));
    CHECK(s.holding());
    CHECK_EQ(s.flush(), std::string("MOS"));
    CHECK(!s.holding());
}

TEST(leaves_other_text_alone) {
    for (const std::string& text : std::vector<std::string>{
             "MOSH CONNECT 60001 tooShortForAKey\r\n",
             "MOSH CONNECT 60001 " + key + "x\r\n",       // 23 characters
             "MOSH CONNECT 0 " + key + "\r\n",            // no port 0
             "MOSH CONNECT 70000 " + key + "\r\n",        // no port 70000
             "MOSH CONNECT 600011 " + key + "\r\n",       // six digits
             "MOSH CONNECTION 60001 " + key + "\r\n",
             "MOSH CONNECT 60001\r\n" + key + "\r\n",     // broken across lines
             "MOSH CONNECT  60001 " + key + "\r\n",       // two spaces
             "MOSH CONNECT 60001 " + key.substr(0, 21) + "\x07" + key.substr(21) + "\r\n",
             "Mosh connect 60001 " + key + "\r\n",
         }) {
        std::optional<MoshConnect> found;
        CHECK_EQ(scan({text}, &found), text);
        CHECK(!found);
    }
    // A key at the very end of the output still counts.
    std::optional<MoshConnect> found;
    CHECK_EQ(scan({"MMOSH CONNECT 1 " + key}, &found), "M" + std::string(15 + 22, ' '));
    CHECK(found && found->port == "1");
}

TEST(drops_console_titles) {
    // The console host announces the program's path as the window title.
    CHECK_EQ(scan({"\x1b]0;C:\\Windows\\System32\\OpenSSH\\ssh.exe\x07" "text"}), std::string("text"));
    CHECK_EQ(scan({"\x1b]2;title\x1b\\x"}), std::string("x"));
    // Other OSC sequences pass.
    CHECK_EQ(scan({"\x1b]8;;http://x\x07link"}), std::string("\x1b]8;;http://x\x07link"));
}

TEST(remembers_the_last_line) {
    ConnectScanner s;
    scan(s, {"Welcome\r\n", "bash: line 1: mosh-server: command not found\r\n", "\x1b[?25h\r\n  \r\n"});
    CHECK_EQ(s.last_line(), std::string("bash: line 1: mosh-server: command not found"));

    ConnectScanner forward;
    scan(forward, {"\x1b[2J\x1b[H", "ssh:\x1b[1Cconnect\x1b[1Cto\x1b[1Chost\x1b[1Cx\x1b[1Cport\x1b[1C22:\x1b[1CConnection",
                   " refused\x1b[K\x1b[?25h"});
    CHECK_EQ(forward.last_line(), std::string("ssh: connect to host x port 22: Connection refused"));

    ConnectScanner moved;
    scan(moved, {"first\x1b[2;1Hsecond\x1b[3;1H"});
    CHECK_EQ(moved.last_line(), std::string("second"));

    ConnectScanner none;
    scan(none, {"\x1b[?25l\x1b[?25h"});
    CHECK(none.last_line().empty());
}

TEST(builds_the_ssh_command_lines) {
    Options o = options_for(
        "mosh://ann@server.example.com:2222?udp=60000:61000&server=%2Fopt%2Fmosh%2Fbin%2Fmosh-server&"
        "ssh_args=-J%20jump.example.com%20-o%20%22SetEnv%3DA%3Db%20c%22");
    CHECK(bootstrap_arguments(o) == (Args{"-t", "-p", "2222", "-J", "jump.example.com", "-o", "SetEnv=A=b c",
                                          "ann@server.example.com", "--", "/opt/mosh/bin/mosh-server", "new", "-s",
                                          "-c", "256", "-l", "LANG=en_US.UTF-8", "-p", "60000:61000"}));
    CHECK(config_arguments(o) ==
          (Args{"-G", "-p", "2222", "-J", "jump.example.com", "-o", "SetEnv=A=b c", "ann@server.example.com"}));

    Options plain = options_for("mosh://[fd00::5]");
    CHECK(bootstrap_arguments(plain) ==
          (Args{"-t", "fd00::5", "--", "mosh-server", "new", "-s", "-c", "256", "-l", "LANG=en_US.UTF-8"}));

    // On the command line, every argument comes back to ssh as it was.
    std::wstring line = command_line(L"C:\\Windows\\System32\\OpenSSH\\ssh.exe", bootstrap_arguments(o));
    CHECK(line == std::wstring(L"C:\\Windows\\System32\\OpenSSH\\ssh.exe -t -p 2222 -J jump.example.com -o "
                               L"\"SetEnv=A=b c\" ann@server.example.com -- /opt/mosh/bin/mosh-server new -s -c 256 "
                               L"-l LANG=en_US.UTF-8 -p 60000:61000"));
    std::wstring spaced = command_line(L"C:\\Program Files\\OpenSSH\\ssh.exe", {"-i", "C:\\keys dir\\id"});
    CHECK(spaced == std::wstring(L"\"C:\\Program Files\\OpenSSH\\ssh.exe\" -i \"C:\\keys dir\\id\""));
}

TEST(reads_ssh_config_output) {
    SshConfig c = parse_ssh_config(
        "user ann\r\nhostname 192.0.2.7\r\nport 2222\r\naddressfamily inet6\r\n"
        "proxycommand none\r\nproxyjump jump.example.com\r\nsetenv A=b c\r\n");
    CHECK_EQ(c.hostname, std::string("192.0.2.7"));
    CHECK_EQ(c.port, std::string("2222"));
    CHECK_EQ(c.address_family, std::string("inet6"));
    CHECK_EQ(c.proxy_command, std::string("none"));
    CHECK_EQ(c.proxy_jump, std::string("jump.example.com"));
    SshConfig empty = parse_ssh_config("garbage\n");
    CHECK(empty.hostname.empty());
    CHECK_EQ(empty.address_family, std::string("any"));
}

TEST(resolves_like_ssh) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    std::string error;
    auto v4 = resolve_like_ssh("127.0.0.1", "any", error);
    REQUIRE(v4);
    CHECK_EQ(*v4, std::string("127.0.0.1"));
    auto local = resolve_like_ssh("localhost", "inet", error);
    REQUIRE(local);
    CHECK_EQ(*local, std::string("127.0.0.1"));
    CHECK(!resolve_like_ssh("127.0.0.1", "inet6", error));  // no IPv6 address for it
    CHECK(!resolve_like_ssh("no-such-host.invalid", "any", error));
    CHECK(!error.empty());
}
CHECK_MAIN
