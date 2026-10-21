// A mosh connection in Casement's terminal view (protocol 2, P7): the ssh
// bootstrap in the terminal, then mosh's client, on a thread of its own.

#pragma once

#include <memory>

#include "casement/plugin.h"
#include "mosh/options.h"

namespace casement::mosh {

/// Starts the session for a `connect` whose address has been read into
/// `options`. `cols`, `rows` are the terminal's size from `connect`.
std::unique_ptr<Session> start_session(Host& host, Options options, int cols, int rows);

}  // namespace casement::mosh
