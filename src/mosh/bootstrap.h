// The bootstrap: ssh runs `mosh-server new` on the remote host in a pseudo
// console that the user sees and types into through the terminal view, so
// ssh's own prompts (host key, password, passphrase, two-factor codes) work
// as in a plain ssh session, until mosh-server has said where to connect.

#pragma once

#include <deque>
#include <functional>
#include <string>

#include "mosh/connect_scanner.h"
#include "mosh/events.h"
#include "mosh/options.h"

namespace casement::mosh {

struct BootstrapResult {
    enum class End { started, failed, cancelled };
    End end = End::failed;
    MoshConnect connect;                // started: where mosh-server listens
    std::string message;                // failed: why, for the user
    std::deque<TerminalEvent> pending;  // started: what arrived after the line, for mosh
    int cols = 80;                      // the terminal's size by the end
    int rows = 24;
};

/// Runs `ssh` with bootstrap_arguments(options) in a pseudo console of
/// cols x rows. Its output goes to `output`, with the MOSH CONNECT line
/// blanked out; input and size changes from `events` go to it. Returns when
/// ssh has exited, or at once when `events` asks to shut down.
BootstrapResult bootstrap(const std::wstring& ssh, const Options& options, int cols, int rows, EventQueue& events,
                          const std::function<void(const std::string&)>& output);

}  // namespace casement::mosh
