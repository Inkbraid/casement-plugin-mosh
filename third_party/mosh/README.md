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

The files are unchanged copies.
