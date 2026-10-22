// casement-mosh: mosh connections in Casement's terminal view (plugin
// protocol 2, proposal P7). See README.md and docs/terminal-view.md.

#include <winsock2.h>

#include <csignal>
#include <cstdlib>
#include <memory>
#include <string>

#include "casement/address.h"
#include "casement/log.h"
#include "casement/plugin.h"
#include "mosh/options.h"
#include "mosh/session.h"

using namespace casement;

namespace {

Host* the_host = nullptr;

// mosh's code checks its invariants with assert() and abort(), as mosh
// itself is built. Tell Casement why the tab ended instead of just dying.
void on_abort(int) {
    if (the_host)
        the_host->output().try_send(
            state_message(State::failed, "The mosh client stopped on an internal error (see Casement's log)."));
    _exit(3);
}

int dimension(const json& msg, const char* key, int fallback, int max) {
    auto v = get_int(msg, key, fallback);
    return v >= 1 && v <= max ? static_cast<int>(v) : fallback;
}

}  // namespace

int main() {
    // No error dialogs from a program without a window: assertion messages
    // go to stderr (Casement's log), crashes end the process quietly.
    _set_error_mode(_OUT_TO_STDERR);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    std::signal(SIGABRT, on_abort);

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 2;

    // Protocol 2 only: a Casement that offers 1 gets "2" back in hello and
    // reports that it needs an update.
    Info info{"MOSH", "Mosh", PLUGIN_VERSION, {}, 2, 2};
    return run(info, [](Host& host, const Connect& c) -> std::unique_ptr<Session> {
        the_host = &host;
        std::string error;
        auto address = parse_address(c.address, error);
        if (!address) host.finish(State::failed, error);
        auto options = mosh::parse_options(*address, c.username, error);
        if (!options) host.finish(State::failed, error);
        int cols = dimension(c.message, "cols", 80, 4096);
        int rows = dimension(c.message, "rows", 24, 1024);
        std::string term = get_string(c.message, "term");
        if (!term.empty() && term != "xterm-256color")
            log::info("the terminal is {}; mosh's output is for xterm-256color", term);
        log::info("connecting to {} ({}x{}, local echo {})", address->url_host(), cols, rows,
                  mosh::to_string(options->predict));
        return mosh::start_session(host, std::move(*options), cols, rows);
    });
}
