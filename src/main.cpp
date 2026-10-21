// casement-mosh: mosh connections in Casement's terminal view (plugin
// protocol 2, proposal P7). See README.md and docs/terminal-view.md.

#include <memory>
#include <string>

#include "casement/address.h"
#include "casement/plugin.h"
#include "mosh/options.h"

using namespace casement;

int main() {
    // Protocol 2 only: a Casement that offers 1 gets "2" back in hello and
    // reports that it needs an update.
    Info info{"MOSH", "Mosh", PLUGIN_VERSION, {}, 2, 2};
    return run(info, [](Host& host, const Connect& c) -> std::unique_ptr<Session> {
        std::string error;
        auto address = parse_address(c.address, error);
        if (!address) host.finish(State::failed, error);
        auto options = mosh::parse_options(*address, c.username, error);
        if (!options) host.finish(State::failed, error);
        host.finish(State::failed, "Mosh connections aren't implemented yet.");
    });
}
