# mosh client sources

The client half of [mosh](https://mosh.org/), the mobile shell, version
**1.4.0**: Copyright 2012 Keith Winstein and the mosh developers (see
[AUTHORS](AUTHORS)), licensed under the GNU General Public License, version
3 or later ([COPYING](COPYING)), with the OpenSSL linking exception stated
in each source file. `src/crypto/ocb_internal.cc` is Ted Krovetz's OCB3
reference code under its own permissive licence (in the file);
[ocb-license.html](ocb-license.html) is Phillip Rogaway's patent grant for
OCB in GPL software, which mosh ships alongside it.

Source: the `mosh-1.4.0` tag of
[mobile-shell/mosh](https://github.com/mobile-shell/mosh) (commit
`bc73a26316ede2a79259d859f8ee309b32412420`), identical to the files in the
release tarball `mosh-1.4.0.tar.gz` (SHA-256
`872e4b134e5df29c8933dff12350785054d2fd2839b5ae6b5587b14db1465ddd`).

Only the files the client needs are here, in mosh's own layout under `src/`:

| Directory | Files | What they do |
|---|---|---|
| `crypto` | `ae.h`, `base64.*`, `byteorder.h`, `crypto.*`, `ocb_internal.cc`, `prng.h` | AES-128-OCB sessions, keys, nonces |
| `network` | `compressor.*`, `network.*`, `networktransport*.h`, `transportfragment.*`, `transportsender*.h`, `transportstate.h` | UDP datagrams, roaming, SSP state synchronisation |
| `protobufs` | `hostinput.proto`, `transportinstruction.proto`, `userinput.proto` | the wire messages |
| `statesync` | `completeterminal.*`, `user.*` | the states SSP synchronises: the screen and the user's keystrokes |
| `terminal` | `parser*`, `terminal*` | the terminal emulator that keeps the client's copy of the screen, and `Display`, which turns screen changes into escape sequences |
| `frontend` | `stmclient.*`, `terminaloverlay.*` | the client's main loop, local echo (prediction) and the notification bar |
| `util` | `dos_assert.h`, `fatal_assert.h`, `shared.h`, `timestamp.*` | helpers |

## Changes for Windows

The plugin builds these files with MinGW-w64 (and MSVC), not on Unix, so
some of them are changed; each changed file says so at the top, and the
repository's history has the unchanged copies (commit "Mosh: client
sources of mosh 1.4.0, unchanged"). The changes:

- `network/network.*`, `network/networktransport.h`: Winsock instead of BSD
  sockets. Sockets are `SOCKET` handles, moved instead of `dup()`ed;
  non-blocking with `ioctlsocket()` instead of `MSG_DONTWAIT`; ICMP errors
  don't fail `recvfrom()` (`SIO_UDP_CONNRESET`); client sockets are bound at
  once so they can be waited on; explicit congestion notification (`IP_TOS`,
  `IP_RECVTOS`) is left out; errors are Windows' messages.
- `frontend/stmclient.*`: the client's terminal is an `STMFrontend`, which
  the plugin connects to Casement's terminal view, instead of the process's
  tty. No
  termios, signals or window size `ioctl`: input, size changes and the
  request to end come as frontend events, and the main loop waits with
  `WaitForMultipleObjects()` on the sockets' event (`WSAEventSelect`) and
  the frontend's, finding readable sockets with `WSAPoll()`, instead of
  `pselect()`. No suspend (Ctrl-Z after the escape key). `shutdown()`
  returns mosh-client's parting message instead of printing it.
- `terminal/terminaldisplayinit.cc`: no terminfo; fixed capabilities of
  xterm-256color, which Casement's terminal emulates.
- `terminal/parser*`, `terminal/terminal.cc`, `terminal/terminalframebuffer.h`,
  `terminal/terminaldisplay.h`, `terminal/terminaldispatcher.h`,
  `frontend/terminaloverlay.*`: characters are `char32_t` (Windows' `wchar_t`
  has 16 bits), UTF-8 is decoded and encoded by the plugin's
  `mosh_utf8_decode()`/`mosh_utf8_append()` instead of the locale-dependent
  `mbrtowc()`/`wcrtomb()`, widths come from `mosh_wcwidth()` (Windows has no
  `wcwidth()`; the plugin's copies glibc's), and notification text is put
  together without `swprintf()`. `shared_ptr::unique()`, deprecated, is
  replaced.
- `crypto/prng.h`: random bytes from `BCryptGenRandom()` instead of
  `/dev/urandom`.
- `crypto/crypto.cc`: no core dump limits on Windows.
- `crypto/byteorder.h`: byte swapping with the C runtime's intrinsics.
- `crypto/ocb_internal.cc`: compiles as C++ with MSVC (`inline` and
  `restrict` left alone, so OpenSSL's `__declspec(restrict)` stays intact; no
  SSSE3 assumed; the right type for `_BitScanForward()`).
- `util/timestamp.cc`: the clock is `QueryPerformanceCounter()`.
- `terminal/terminaldispatcher.cc`, `terminal/terminalfunctions.cc`:
  `[[maybe_unused]]` instead of GCC's `__attribute((unused))`.

What Windows lacks entirely is in the plugin's `src/mosh/port`: the build
configuration (`config.h`), the protocol buffers messages (hand-written
`*.pb.h`, see below), UTF-8 and `mosh_wcwidth()`, and the random source.

The `.proto` files are here for reference: the plugin doesn't link
libprotobuf. mosh's three message files are small and fixed, so
`src/mosh/port/*.pb.h` implement the part of protoc's generated API that
mosh's code calls, writing the same bytes as protobuf 3.21 (checked against
protoc's output in `tests/unit/port_tests.cpp`).
