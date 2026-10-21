# Mosh support for Casement

A protocol plugin for Casement that opens `mosh://` connections in a
terminal tab. It starts `mosh-server` on the remote host through Windows'
own ssh, with ssh's prompts in the tab, and then runs a native Windows port
of the mosh client, so the shell survives network changes, sleep and slow
links, with mosh's local echo on high-latency connections.

This is an independent project. It is not made or endorsed by the Mosh
project; "Mosh" names the software it works with.

## Status

| Feature | Status |
|---|---|
| Opening a tab in Casement | Needs protocol v2: the terminal view, proposal P7 ([docs/terminal-view.md](docs/terminal-view.md)) |
| Starting mosh-server through `ssh.exe` in a pseudo console, ssh's prompts in the tab | Done |
| The `MOSH CONNECT` line kept off the screen | Done |
| mosh client: SSP over UDP, AES-128-OCB | Done (mosh 1.4.0's code) |
| Screen updates as xterm-256color sequences, mosh's notification bar | Done |
| Local echo: adaptive, always, never | Done |
| Resizing the remote terminal with the tab | Done |
| Ending the session: shell exits, Ctrl-^ `.`, closing the tab | Done |
| UDP port or range, mosh-server command, extra ssh options (jump hosts) | Done |
| IPv6 | Done; not yet tested end to end |
| Roaming between networks, resuming after sleep | Mosh's own code; not yet tested with this plugin |
| Scrollback | Not in mosh (see [Things to know](#things-to-know)) |
| Suspending the client (Ctrl-^ Ctrl-Z) | Not applicable: there is no shell around the client |

## Installing

Download `casement-mosh-<version>-windows-x64.zip` from the
[Releases](https://github.com/Inkbraid/casement-plugin-mosh/releases) and
unpack its `mosh` folder into `%LOCALAPPDATA%\Casement\plugins\`, so that
`%LOCALAPPDATA%\Casement\plugins\mosh\plugin.json` exists. Casement lists
the connection type after a restart.

The plugin needs Windows 10 version 1809 or later and Windows' OpenSSH
client, an optional feature that most installations have (Settings >
System > Optional features on Windows 11, Settings > Apps > Optional
features on Windows 10). It runs `ssh.exe` from
`%SystemRoot%\System32\OpenSSH`, or from the `PATH` if it isn't there.

## Connections

| Field | In the address | Meaning |
|---|---|---|
| User | `user@` | The user on the server. Empty: whatever ssh uses (your ssh configuration's `User`, or your Windows user name). |
| Host | host | The server as ssh knows it: a name, an address, or a `Host` from your ssh configuration. |
| SSH port | `:port` | ssh's port. Empty: ssh's default (22, or your configuration's `Port`). |
| UDP port or range | `udp=` | The UDP port (`60001`) or range (`60000:61000`) mosh-server may use; mosh-server's `-p`. Empty: mosh-server's default, 60000 to 61000. |
| mosh-server command | `server=` | What ssh runs on the server instead of `mosh-server`, for example `/usr/local/bin/mosh-server` or `env LC_ALL=C.UTF-8 mosh-server`. |
| Local echo | `predict=` | `adaptive` (show typing early when the link is slow; the default), `always` or `never`, like mosh's `--predict`. |
| Extra ssh options | `ssh_args=` | Options for ssh, written as on a Windows command line, for example `-J jump.example.com` or `-o "ServerAliveInterval 30"`. |

An address is

```
mosh://[user@]host[:ssh-port][?udp=…&server=…&predict=…&ssh_args=…]
```

with the values percent-encoded and IPv6 addresses in brackets. Examples:

```
mosh://ann@server.example.com
mosh://ann@server.example.com:2222?udp=60001
mosh://ann@[2001:db8::5]?predict=always
mosh://ann@internal.example.com?ssh_args=-J%20jump.example.com
mosh://pi@raspberrypi.local?server=%2Fusr%2Flocal%2Fbin%2Fmosh-server
```

The connection has no password field: ssh asks for passwords, passphrases
and codes itself, in the tab.

## How it works

1. **Bootstrap.** The plugin starts ssh in a Windows pseudo console (ConPTY)
   of the tab's size and connects the two: what ssh shows appears in the
   tab, what you type goes to ssh, and resizing the tab resizes the
   console. So host key questions, passwords, key passphrases and
   two-factor prompts work as in a console. ssh runs in your user folder
   with your ssh configuration, keys, agent and `known_hosts`, as

   ```
   ssh -t [-p port] [extra options] [user@]host -- mosh-server new -s -c 256 -l LANG=en_US.UTF-8 [-p udp]
   ```

   much as mosh's own `mosh` command does.
2. **Handover.** mosh-server prints `MOSH CONNECT <port> <key>` and detaches
   from ssh. The plugin finds the line in the console's output and replaces
   it with spaces, so the session key never reaches the screen, and waits
   for ssh to exit (ending it after 10 seconds). Keys you type meanwhile are
   kept for mosh.
3. **The server's address.** mosh-server listens on the address that ssh
   connected to (`-s`), so mosh has to send to that address. The plugin asks
   ssh with `ssh -G` how it sees the host, which applies `HostName`,
   `AddressFamily`, `-4` and `-6` from your configuration and the extra
   options, and resolves that name as ssh does, taking the first address.
4. **Session.** The plugin's mosh client takes over the tab. It switches the
   terminal to the alternate screen and application cursor keys and draws
   the screen as mosh-server sends it, its notification bar ("Last contact
   12 seconds ago") and local echo included, as escape sequences for
   xterm-256color, the terminal Casement emulates. Your keystrokes go to
   mosh-server, and resizing the tab resizes the remote terminal. mosh's
   escape key works as in mosh: Ctrl-^ then `.` ends the session, Ctrl-^
   then `^` types a Ctrl-^.
5. **End.** The tab reports *closed* when the remote shell exits or you end
   the session with Ctrl-^ `.`. Closing the tab asks mosh-server to end the
   session, as Ctrl-^ `.` does. The tab reports *failed*, with the reason,
   when ssh couldn't start mosh-server (the text is the last line ssh
   showed, such as `bash: mosh-server: command not found`), when the
   server's name can't be resolved, or when nothing comes back over UDP for
   about 20 seconds (mosh's "Nothing received from server" notice shows
   meanwhile).

The protocol messages are described in
[docs/terminal-view.md](docs/terminal-view.md).

## Things to know

- **Casement version.** The plugin needs a Casement with the terminal view
  (protocol version 2, P7). A Casement that only knows version 1 lists the
  plugin as needing a newer Casement; if it starts the plugin anyway, the
  plugin answers that it speaks protocol 2 and does nothing else.
- **No scrollback.** mosh synchronises the screen, not the stream of
  output, and skips intermediate screens when output is fast. Casement's
  scrollback holds only the lines that scrolled away while mosh drew, with
  gaps. Run tmux or screen on the server for a dependable history.
- **The server needs** mosh-server (the `mosh` package on most systems),
  found by non-interactive shells or named with `server=`; a UTF-8 locale:
  mosh-server uses the server's default if that is UTF-8, and otherwise
  `en_US.UTF-8`, which the plugin passes where mosh's `mosh` command
  passes your own locale; and the UDP ports open in its firewall: 60000 to
  61000 unless you set `udp=`. A server behind NAT needs the UDP port
  forwarded too; set `udp=` to that port.
- **One address.** mosh's UDP goes to the address the plugin resolved (see
  above). When a name has several addresses and ssh had to fall back from
  the first (an IPv6 address that doesn't work, say), mosh-server listens on
  an address mosh doesn't send to, and the tab shows "Nothing received from
  server". Add `-4` or `-6` to the extra ssh options, set `AddressFamily` in
  your ssh configuration, or use the address itself.
- **Jump hosts.** ssh can reach the server through `-J`, `ProxyJump` or a
  `ProxyCommand` in your configuration, but mosh's UDP goes straight from
  this computer to the server's name as resolved here: the server must be
  reachable over UDP directly.
- **Closing the tab ends the session.** Losing the network or the computer
  sleeping doesn't: the session continues when packets get through again.
  If the plugin can't reach mosh-server when the tab closes, mosh-server
  keeps the session until its shell exits or for the time set in
  `MOSH_SERVER_NETWORK_TMOUT` on the server, as with any mosh client that
  went away.
- **Addresses from others.** An address can carry a mosh-server command and
  ssh options. The mosh-server command runs on the server under your
  account once you log in, so check addresses you didn't write yourself.
  ssh options that make ssh run programs, load libraries or write files on
  this computer are refused (`-E`, `-F`, `-I`, and `-o` with
  `ProxyCommand`, `LocalCommand`, `PermitLocalCommand`,
  `KnownHostsCommand`, `PKCS11Provider`, `SecurityKeyProvider` or
  `XAuthLocation`); your own ssh configuration can still use them. Host and
  user names with shell metacharacters are refused, as OpenSSH 9.6 refuses
  them (Windows comes with an older OpenSSH).
- **Environment variables**, in the environment Casement runs in:
  `MOSH_ESCAPE_KEY` changes the escape key (empty turns it off),
  `MOSH_TITLE_NOPREFIX` drops the `[mosh]` prefix from window titles,
  `MOSH_NO_TERM_INIT` keeps the session off the alternate screen,
  `CASEMENT_MOSH_SSH` names another `ssh.exe`, and `CASEMENT_MOSH_LOG` sets
  the level of the plugin's log in Casement's log (`error`, `warn`, `info`
  (the default), `debug` or `trace`; `trace` adds mosh's own diagnostics).

## Building

The plugin is C++20 (mosh's code C++17) and builds with CMake 3.24 or later.
OpenSSL 3 and zlib are linked statically, so the result is one exe without
DLLs.

**MSVC** (Visual Studio 2022), with [vcpkg](https://vcpkg.io/): `vcpkg.json`
pins OpenSSL 3.5.4 and zlib 1.3.1 on a builtin-baseline, so vcpkg needs to
be a full clone that has that commit. In a Developer PowerShell:

```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build
ctest --test-dir build -L unit
```

**MinGW-w64** cross build on Linux (Ubuntu 24.04: `mingw-w64`, `cmake`,
`ninja-build`), with static zlib and OpenSSL built once into a prefix:

```sh
ci/mingw-deps.sh ~/mingw64 zlib openssl
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake -DCMAKE_PREFIX_PATH=$HOME/mingw64
cmake --build build
```

`build/casement-mosh.exe` is the plugin; `plugin.json` and `icon.png` go
next to it.

## Testing

**Unit tests** (`ctest --test-dir build -L unit`; a MinGW build runs them
under Wine): the `mosh://` options and their checks, Windows command line
splitting and quoting, the `MOSH CONNECT` scanner (lines split across
reads, console host output, held-back text), the ssh command lines and
`ssh -G` output, address resolution, AES-128-OCB against RFC 7253's test
vectors, mosh's session encryption, the protocol buffer messages against
protoc's output, the random source and the clock, UTF-8 decoding and
character widths against glibc, and the screen output for known screen
states.

**End-to-end tests** run the real plugin exe under Wine through
`tools/sim` (Casement's side of the protocol) against a real mosh-server:

```sh
ci/e2e.sh build
```

They need Wine, mosh-server, python3 and setsid; with OpenSSH's ssh and
sshd installed, one test runs those too. `tests/e2e/fake-ssh.exe` stands in
for `ssh.exe` (the plugin runs it through `CASEMENT_MOSH_SSH`) and passes
the remote command to `tests/e2e/fake-sshd.py`, which runs it in a pseudo
terminal the way sshd does, can put a delaying UDP relay between the plugin
and mosh-server or cut the link for a while, and hands connections from the
real ssh to sshd. The script uses TCP port 2222 and UDP ports 61000 to
61099. The tests cover: protocol version 1 answered with 2 and nothing
else; a bad address; a missing `ssh.exe`; the bootstrap output with the
`MOSH CONNECT` line hidden, the shell's prompt, typed commands, UTF-8 and
wide characters, and *closed* when the shell exits; resizing, also while
ssh asks for a password; password prompts in the terminal; *failed* with
ssh's last line (wrong passwords, no mosh-server); closing the tab ending
mosh-server; Ctrl-^ `.`; the three local echo modes over a slow link;
mosh's notice when the link dies, and recovery; extra ssh options passed
as typed; no answer over UDP; IPv6 (skipped without an IPv6 loopback); and
the real OpenSSH client and server, with a host name only ssh's
configuration knows.

## Protocol v2

The plugin needs **P7**, the terminal view, and nothing else: ssh's prompts
appear in the terminal, so prompts (P2) aren't needed, and mosh's
notification bar takes the place of status text (P1). P7 as this plugin
implements it is written up in [docs/terminal-view.md](docs/terminal-view.md).

## Licensing

The plugin is licensed under the GNU General Public License, version 3 or
later, with the OpenSSL linking exception mosh uses; see
[LICENSE](LICENSE). Release zips hold the exe with its manifest, icon and
documents; the corresponding source is each release's source archive.

Third-party components:

| Component | Version | Licence | Where |
|---|---|---|---|
| [mosh](https://mosh.org/) client sources (Copyright 2012 Keith Winstein and the mosh developers) | 1.4.0 | GPL-3.0-or-later with the OpenSSL exception | `third_party/mosh`, changed for Windows as its [README](third_party/mosh/README.md) lists |
| OCB3 reference code (Ted Krovetz), part of mosh | as in mosh 1.4.0 | ISC-style, in the file; Phillip Rogaway's patent grant for GPL software in `third_party/mosh/ocb-license.html` | `third_party/mosh/src/crypto/ocb_internal.cc` |
| [OpenSSL](https://www.openssl.org/) | 3.5.4 | Apache-2.0 | linked statically; built by vcpkg (MSVC) or `ci/mingw-deps.sh` (MinGW) |
| [zlib](https://zlib.net/) | 1.3.1 | zlib licence | linked statically; built the same way |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.12.0 | MIT | `third_party/nlohmann` |

`src/mosh/port/wcwidth_table.h` lists character widths as glibc 2.39's
`wcwidth()` reports them (generated by `tools/gen-wcwidth.py`), so the
client measures text as Linux servers do.

`docs/plugin-protocol.md` is a copy of the Casement plugin protocol from
[Inkbraid/casement-plugin-protocol](https://github.com/Inkbraid/casement-plugin-protocol),
licensed under CC BY 4.0.
