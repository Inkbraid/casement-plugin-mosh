#include "mosh/bootstrap.h"

#include <algorithm>
#include <chrono>
#include <mutex>

#include "casement/log.h"
#include "casement/text.h"
#include "mosh/conpty.h"
#include "mosh/ssh.h"

namespace casement::mosh {

namespace {

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

// Text that may be the start of the MOSH CONNECT line is shown after this
// long without more output. mosh-server writes the line in one piece.
constexpr milliseconds flush_delay(200);
// mosh-server has detached once it printed the line, so ssh ends soon after.
constexpr milliseconds exit_wait(10000);
// The console host's last output after it is closed.
constexpr milliseconds close_wait(3000);

int clamp_size(int v) { return std::clamp(v, 1, 32767); }

std::wstring home_directory() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    return n > 0 && n < MAX_PATH ? std::wstring(buf, n) : std::wstring();
}

DWORD until(Clock::time_point t) {
    auto left = std::chrono::duration_cast<milliseconds>(t - Clock::now()).count();
    return left <= 0 ? 0 : static_cast<DWORD>(left);
}

/// Reads the pseudo console's output on its own thread (the pipe has no
/// timeouts), handing chunks over through a queue.
class Reader {
public:
    explicit Reader(HANDLE pipe) : pipe_(pipe), event_(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {
        thread_ = CreateThread(nullptr, 0, &Reader::main, this, 0, nullptr);
        if (!thread_) {
            std::lock_guard g(lock_);
            ended_ = true;
        }
    }
    ~Reader() {
        stop();
        if (event_) CloseHandle(event_);
    }
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;

    HANDLE event() const { return event_; }

    /// The chunks so far; `ended` once the pipe is closed.
    std::deque<std::string> take(bool& ended) {
        std::lock_guard g(lock_);
        std::deque<std::string> out;
        out.swap(chunks_);
        ended = ended_;
        return out;
    }

    /// Waits for the thread, cutting a read short after `wait`.
    void stop(milliseconds wait = milliseconds(0)) {
        if (!thread_) return;
        if (WaitForSingleObject(thread_, static_cast<DWORD>(wait.count())) == WAIT_TIMEOUT) {
            CancelSynchronousIo(thread_);
            WaitForSingleObject(thread_, INFINITE);
        }
        CloseHandle(thread_);
        thread_ = nullptr;
    }

private:
    static DWORD WINAPI main(void* self) {
        auto* r = static_cast<Reader*>(self);
        char buf[8192];
        DWORD n = 0;
        while (ReadFile(r->pipe_, buf, sizeof(buf), &n, nullptr) && n > 0) {
            {
                std::lock_guard g(r->lock_);
                r->chunks_.emplace_back(buf, n);
            }
            SetEvent(r->event_);
        }
        {
            std::lock_guard g(r->lock_);
            r->ended_ = true;
        }
        SetEvent(r->event_);
        return 0;
    }

    HANDLE pipe_;
    HANDLE event_;
    HANDLE thread_ = nullptr;
    std::mutex lock_;
    std::deque<std::string> chunks_;
    bool ended_ = false;
};

}  // namespace

BootstrapResult bootstrap(const std::wstring& ssh, const Options& options, int cols, int rows, EventQueue& events,
                          const std::function<void(const std::string&)>& output) {
    BootstrapResult r;
    r.cols = cols;
    r.rows = rows;
    std::wstring cmd = command_line(ssh, bootstrap_arguments(options));
    log::info("starting {}", narrow(cmd));
    std::string error;
    auto pc = PseudoConsole::start(ssh, cmd, home_directory(), clamp_size(cols), clamp_size(rows), error);
    if (!pc) {
        r.message = error;
        return r;
    }

    ConnectScanner scanner;
    Reader reader(pc->output());
    Clock::time_point flush_at{}, exit_by{};
    bool ended = false;

    auto show = [&](const std::string& bytes) {
        if (!bytes.empty()) output(bytes);
    };
    // Passes on what the console wrote; true if any arrived.
    auto drain = [&] {
        auto chunks = reader.take(ended);
        for (const std::string& chunk : chunks) show(scanner.feed(chunk));
        if (!chunks.empty() && scanner.holding()) flush_at = Clock::now() + flush_delay;
        return !chunks.empty();
    };

    bool exited = false, cancelled = false;
    while (!exited && !cancelled) {
        DWORD timeout = INFINITE;
        if (scanner.holding()) timeout = until(flush_at);
        if (scanner.connect()) timeout = std::min(timeout, until(exit_by));
        HANDLE handles[3] = {pc->process(), events.event(), reader.event()};
        WaitForMultipleObjects(3, handles, FALSE, timeout);

        bool had_line = scanner.connect().has_value();
        drain();
        if (scanner.holding() && Clock::now() >= flush_at) show(scanner.flush());
        if (!had_line && scanner.connect()) {
            log::info("mosh-server listens on UDP port {}", scanner.connect()->port);
            exit_by = Clock::now() + exit_wait;
        }

        for (TerminalEvent& e : events.take()) {
            if (e.type == TerminalEvent::shutdown) {
                cancelled = true;
                break;
            }
            if (e.type == TerminalEvent::resize) {
                r.cols = e.cols;
                r.rows = e.rows;
            }
            if (scanner.connect()) {
                // ssh is on its way out; this is for mosh.
                r.pending.push_back(std::move(e));
            } else if (e.type == TerminalEvent::input) {
                pc->write(e.bytes);
            } else {
                pc->resize(clamp_size(e.cols), clamp_size(e.rows));
            }
        }

        if (WaitForSingleObject(pc->process(), 0) == WAIT_OBJECT_0) {
            exited = true;
        } else if (scanner.connect() && Clock::now() >= exit_by) {
            log::warn("ssh is still running {} s after mosh-server started; ending it",
                      std::chrono::duration_cast<std::chrono::seconds>(exit_wait).count());
            TerminateProcess(pc->process(), 1);
            exited = true;
        }
    }

    // Closing the console ends ssh if it still runs; the console host then
    // writes what it hasn't yet and closes the pipe.
    pc->close();
    auto close_by = Clock::now() + close_wait;
    while (!ended && Clock::now() < close_by) {
        WaitForSingleObject(reader.event(), until(close_by));
        drain();
    }
    reader.stop(milliseconds(100));
    drain();
    show(scanner.flush(true));

    if (cancelled) {
        r.end = BootstrapResult::End::cancelled;
        return r;
    }
    if (scanner.connect()) {
        r.end = BootstrapResult::End::started;
        r.connect = *scanner.connect();
        return r;
    }
    DWORD code = 0;
    std::string last = scanner.last_line();
    if (!last.empty()) r.message = last;
    else if (pc->exit_code(code)) r.message = std::format("ssh exited with code {} without starting mosh-server.", code);
    else r.message = "ssh ended without starting mosh-server.";
    return r;
}

}  // namespace casement::mosh
