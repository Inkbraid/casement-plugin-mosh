#include "mosh/session.h"

#include <atomic>
#include <cstdint>
#include <format>
#include <span>
#include <thread>

#include "casement/log.h"
#include "casement/text.h"
#include "mosh/bootstrap.h"
#include "mosh/client.h"
#include "mosh/events.h"
#include "mosh/ssh.h"

namespace casement::mosh {

namespace {

// Casement ends the process two seconds after `disconnect`; a clean mosh
// shutdown takes a round trip.
constexpr DWORD disconnect_wait_ms = 1500;

// mosh's terminal sizes are bounded by its compression buffer (4 MiB).
constexpr int max_cols = 4096;
constexpr int max_rows = 1024;

json term_output_message(std::string_view bytes) {
    std::span<const std::uint8_t> data(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
    return json{{"type", "term_output"}, {"data", base64_encode(data)}};
}

/// Text for the terminal: line breaks as a terminal needs them.
std::string terminal_text(std::string_view text) {
    std::string out;
    for (char c : text) {
        if (c == '\n') out += '\r';
        out += c;
    }
    return out;
}

class MoshSession : public Session {
public:
    MoshSession(Host& host, Options options, int cols, int rows)
        : host_(host), options_(std::move(options)), cols_(cols), rows_(rows),
          done_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
        thread_ = std::thread([this] { run(); });
    }

    ~MoshSession() override {
        if (thread_.joinable()) thread_.join();
        if (done_) CloseHandle(done_);
    }

    void input(const json& msg) override {
        std::string type = get_string(msg, "type");
        if (type == "term_input") {
            auto data = base64_decode(get_string(msg, "data"));
            if (!data) {
                log::warn("term_input whose data isn't base64; ignored");
                return;
            }
            if (data->empty()) return;
            TerminalEvent e;
            e.type = TerminalEvent::input;
            e.bytes.assign(data->begin(), data->end());
            events_.push(std::move(e));
        } else if (type == "term_resize") {
            auto cols = get_int(msg, "cols", 0), rows = get_int(msg, "rows", 0);
            if (cols < 1 || rows < 1 || cols > max_cols || rows > max_rows) {
                log::warn("term_resize to {}x{} ignored", cols, rows);
                return;
            }
            TerminalEvent e;
            e.type = TerminalEvent::resize;
            e.cols = static_cast<int>(cols);
            e.rows = static_cast<int>(rows);
            events_.push(std::move(e));
        } else {
            log::debug("ignoring {}", type);
        }
    }

    void disconnect() override {
        if (disconnecting_.exchange(true)) return;
        TerminalEvent e;
        e.type = TerminalEvent::shutdown;
        events_.push(std::move(e));
        if (WaitForSingleObject(done_, disconnect_wait_ms) == WAIT_TIMEOUT) {
            log::info("the session didn't end in time; exiting");
            ExitProcess(0);
        }
    }

private:
    void run() {
        try {
            work();
        } catch (const std::exception& e) {
            if (!disconnecting_) finish(State::failed, e.what());
        }
        SetEvent(done_);
    }

    void work() {
        auto output = [this](const std::string& bytes) { send_output(bytes); };

        std::wstring ssh = find_ssh();
        if (ssh.empty())
            finish(State::failed,
                   "ssh.exe wasn't found. Mosh connections need Windows' OpenSSH client (Settings > System > "
                   "Optional features > OpenSSH Client).");
        log::info("using {}", narrow(ssh));

        BootstrapResult b = bootstrap(ssh, options_, cols_, rows_, events_, output);
        if (b.end == BootstrapResult::End::cancelled) return;
        if (b.end == BootstrapResult::End::failed) finish(State::failed, b.message);

        // mosh-server listens on the address ssh connected to (it was told
        // -s), so use that one: ask ssh how it sees the host (after
        // ~/.ssh/config's HostName, AddressFamily, -4/-6) and resolve the
        // name as ssh does.
        std::string error;
        std::string host = options_.host, family = "any";
        if (auto config = query_ssh_config(ssh, options_, error)) {
            if (!config->hostname.empty()) host = config->hostname;
            if (!config->address_family.empty()) family = config->address_family;
            bool proxy = (!config->proxy_jump.empty() && config->proxy_jump != "none") ||
                         (!config->proxy_command.empty() && config->proxy_command != "none");
            if (proxy) log::info("ssh goes through a proxy; mosh needs to reach {} directly over UDP", host);
        } else {
            log::warn("ssh -G failed ({}); resolving {} as it is", error, host);
        }
        auto ip = resolve_like_ssh(host, family, error);
        if (!ip) finish(State::failed, error);
        log::info("mosh-server at {} on UDP port {}", *ip, b.connect.port);

        events_.push_front(std::move(b.pending));
        ClientSettings settings;
        settings.ip = *ip;
        settings.port = b.connect.port;
        settings.key = b.connect.key;
        settings.predict = std::string(to_string(options_.predict));
        settings.cols = b.cols;
        settings.rows = b.rows;
        settings.verbose = log::enabled(log::Level::trace) ? 1 : 0;
        ClientResult r = run_client(settings, events_, output);

        if (disconnecting_) return;  // Casement asked for it
        if (r.server_ended) finish(State::closed, "mosh-server ended the session.");
        if (!r.connected) {
            send_output(terminal_text("\n" + r.mosh_message));
            finish(State::failed, std::format("Nothing came back from mosh-server at {} on UDP port {}. A firewall "
                                              "between here and the server may be blocking UDP.",
                                              *ip, b.connect.port));
        }
        if (!r.clean) {
            send_output(terminal_text("\n" + r.mosh_message));
            finish(State::closed, "The session ended without mosh-server confirming it; mosh-server may still run "
                                  "on the server.");
        }
        finish(State::closed, "The mosh session ended.");
    }

    void send_output(const std::string& bytes) {
        if (bytes.empty()) return;
        if (!connected_sent_) {
            // The terminal shows from the first output on, ssh's prompts
            // included.
            host_.send(state_message(State::connected));
            connected_sent_ = true;
        }
        host_.send(term_output_message(bytes));
    }

    [[noreturn]] void finish(State state, std::string_view text) { host_.finish(state, text); }

    Host& host_;
    Options options_;
    int cols_, rows_;
    EventQueue events_;
    HANDLE done_;
    std::thread thread_;
    std::atomic<bool> disconnecting_{false};
    bool connected_sent_ = false;  // used on the session thread only
};

}  // namespace

std::unique_ptr<Session> start_session(Host& host, Options options, int cols, int rows) {
    return std::make_unique<MoshSession>(host, std::move(options), cols, rows);
}

}  // namespace casement::mosh
