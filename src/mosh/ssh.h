// ssh.exe: where it is, the command line that starts mosh-server through
// it, and the address it connects to.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mosh/options.h"

namespace casement::mosh {

/// The ssh program: the CASEMENT_MOSH_SSH environment variable if set (the
/// tests use it to run a stand-in), else Windows' own OpenSSH client in
/// %SystemRoot%\System32\OpenSSH, else ssh.exe on PATH. Empty if there is
/// none.
std::wstring find_ssh();

/// The arguments for the bootstrap, after the program name:
///   -t [-p port] [ssh_args] [user@]host -- <server> new -s -c 256
///      -l LANG=en_US.UTF-8 [-p udp]
std::vector<std::string> bootstrap_arguments(const Options& options);

/// The arguments for `ssh -G` (print the configuration ssh would use) with
/// the same options and destination.
std::vector<std::string> config_arguments(const Options& options);

/// A command line: the program and each argument quoted for Windows.
std::wstring command_line(const std::wstring& program, const std::vector<std::string>& args);

/// What ssh -G says about the connection.
struct SshConfig {
    std::string hostname;
    std::string port;
    std::string address_family = "any";  // any, inet or inet6
    std::string proxy_jump;
    std::string proxy_command;
};

/// Reads `ssh -G` output.
SshConfig parse_ssh_config(std::string_view text);

/// Runs `ssh -G`. Empty optional, with `error` set, if ssh fails.
std::optional<SshConfig> query_ssh_config(const std::wstring& ssh, const Options& options, std::string& error);

/// Resolves a host name as ssh does (getaddrinfo() with ssh's address
/// family) and returns the first address in numeric form. Empty optional,
/// with `error` set for the user, if it can't be resolved.
std::optional<std::string> resolve_like_ssh(const std::string& host, std::string_view address_family,
                                            std::string& error);

}  // namespace casement::mosh
