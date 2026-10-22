// What arrives from Casement's terminal view while a session runs: typed
// bytes, size changes and the request to end. Casement's messages are read
// on the protocol thread; the session thread takes them from this queue.

#pragma once

#include <windows.h>

#include <deque>
#include <mutex>
#include <string>

namespace casement::mosh {

struct TerminalEvent {
    enum Type { input, resize, shutdown };
    Type type = input;
    std::string bytes;  // input
    int cols = 0;       // resize
    int rows = 0;
};

class EventQueue {
public:
    EventQueue() : event_(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {}
    ~EventQueue() {
        if (event_) CloseHandle(event_);
    }
    EventQueue(const EventQueue&) = delete;
    EventQueue& operator=(const EventQueue&) = delete;

    void push(TerminalEvent e) {
        {
            std::lock_guard<std::mutex> g(lock_);
            events_.push_back(std::move(e));
        }
        SetEvent(event_);
    }
    /// Puts events back in front, oldest first.
    void push_front(std::deque<TerminalEvent> events) {
        if (events.empty()) return;
        {
            std::lock_guard<std::mutex> g(lock_);
            events_.insert(events_.begin(), events.begin(), events.end());
        }
        SetEvent(event_);
    }
    std::deque<TerminalEvent> take() {
        std::lock_guard<std::mutex> g(lock_);
        std::deque<TerminalEvent> out;
        out.swap(events_);
        return out;
    }
    /// Signalled (auto-reset) when something was pushed.
    HANDLE event() const { return event_; }

private:
    HANDLE event_;
    std::mutex lock_;
    std::deque<TerminalEvent> events_;
};

}  // namespace casement::mosh
