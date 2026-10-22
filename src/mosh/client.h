// The mosh session after the bootstrap: mosh's own client loop (STMClient,
// ported to Windows) with Casement's terminal view as the user's terminal.
// Kept free of mosh's headers, which bring their own names into the global
// namespace.

#pragma once

#include <functional>
#include <string>

#include "mosh/events.h"

namespace casement::mosh {

struct ClientSettings {
    std::string ip;    // numeric address of the server
    std::string port;  // mosh-server's UDP port
    std::string key;   // the session key, 22 characters of base64
    std::string predict = "adaptive";
    int cols = 80;
    int rows = 24;
    unsigned verbose = 0;  // mosh's own debugging output on stderr
};

struct ClientResult {
    bool connected = false;     // the server answered at least once
    bool server_ended = false;  // the server ended the session (the shell exited)
    bool clean = false;         // the session ended with an acknowledged shutdown
    std::string mosh_message;   // what mosh-client would print on the way out
};

/// Runs the session until it ends, taking input, size changes and the
/// request to end from `events` and writing terminal output through
/// `output`. Throws std::exception, after restoring the terminal, for the
/// errors mosh-client exits with.
ClientResult run_client(const ClientSettings& settings, EventQueue& events,
                        const std::function<void(const std::string&)>& output);

}  // namespace casement::mosh
