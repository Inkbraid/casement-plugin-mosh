// Built with mosh's sources (C++17, mosh's include paths): the only file of
// the plugin's own that includes mosh's headers.

#include "mosh/client.h"

#include <stdexcept>

#include "stmclient.h"

namespace casement::mosh {

namespace {

class Frontend : public STMFrontend {
public:
    Frontend(EventQueue& events, const std::function<void(const std::string&)>& output)
        : events_(events), output_(output) {}

    void write(const std::string& bytes) override {
        if (!bytes.empty()) output_(bytes);
    }

    HANDLE event(void) override { return events_.event(); }

    std::deque<Event> take_events(void) override {
        std::deque<Event> out;
        for (TerminalEvent& e : events_.take()) {
            Event x;
            x.type = e.type == TerminalEvent::input ? INPUT : e.type == TerminalEvent::resize ? RESIZE : SHUTDOWN;
            x.bytes = std::move(e.bytes);
            x.width = e.cols;
            x.height = e.rows;
            out.push_back(std::move(x));
        }
        return out;
    }

private:
    EventQueue& events_;
    const std::function<void(const std::string&)>& output_;
};

}  // namespace

ClientResult run_client(const ClientSettings& settings, EventQueue& events,
                        const std::function<void(const std::string&)>& output) {
    Frontend frontend(events, output);
    ClientResult result;
    // As mosh-client's main(): the terminal is restored whatever happens.
    std::string what;
    try {
        STMClient client(frontend, settings.cols, settings.rows, settings.ip.c_str(), settings.port.c_str(),
                         settings.key.c_str(), settings.predict.c_str(), settings.verbose, nullptr);
        client.init();
        try {
            result.clean = client.main();
        } catch (...) {
            client.shutdown();
            throw;
        }
        result.connected = !client.still_connecting();
        result.server_ended = client.server_ended();
        result.mosh_message = client.shutdown();
        return result;
    } catch (const Network::NetworkException& e) {
        what = std::string("Network exception: ") + e.what();
    } catch (const Crypto::CryptoException& e) {
        what = std::string("Crypto exception: ") + e.what();
    }
    throw std::runtime_error(what);
}

}  // namespace casement::mosh
