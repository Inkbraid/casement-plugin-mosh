// The parts of a mosh:// address (see plugin.json) and the checks that keep
// them from turning into something else on the ssh command line or in the
// remote command.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "casement/address.h"

namespace casement::mosh {

enum class Predict { adaptive, always, never };

std::string_view to_string(Predict predict);

struct Options {
    std::string user;  // may be empty: ssh then uses its own default
    std::string host;  // IPv6 addresses without brackets
    std::optional<std::uint16_t> ssh_port;
    std::string udp;  // "", "60001" or "60000:61000"
    std::string server = "mosh-server";
    Predict predict = Predict::adaptive;
    std::vector<std::string> ssh_args;  // already split
};

/// Reads the options from a parsed address; `username` from `connect` is
/// used when the address has no user part. On failure `error` is a
/// sentence for the user.
std::optional<Options> parse_options(const Address& address, std::string_view username, std::string& error);

/// Splits text into arguments the way a Windows program splits its command
/// line (CommandLineToArgvW and the C runtime): blanks separate, double
/// quotes group, backslashes are literal except before a quote. Empty
/// optional for an unmatched quote.
std::optional<std::vector<std::string>> split_arguments(std::string_view text);

/// Quotes one argument for a Windows command line so that the program sees
/// exactly `arg`.
std::string quote_argument(std::string_view arg);

/// Checks that `args` holds only ssh options and their values, so nothing
/// in them can become the destination or the remote command. On failure
/// `error` is a sentence for the user.
bool check_ssh_args(const std::vector<std::string>& args, std::string& error);

/// Checks a UDP port or "low:high" range as mosh-server's -p takes it.
bool valid_udp_ports(std::string_view text);

}  // namespace casement::mosh
