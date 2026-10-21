# Terminal view (protocol version 2, proposal P7)

This is proposal P7 from the Casement plugin specs, as this plugin
implements it. It is not part of the protocol until it is added to
[plugin-protocol.md](plugin-protocol.md) (version 1); everything not
mentioned here works as version 1 describes.

Version 1 plugins deliver pictures. A terminal view lets a plugin deliver a
terminal byte stream instead: Casement runs it through the same terminal
emulator as its SSH, telnet, serial and shell tabs (libvterm), so fonts,
themes, selection, copy and paste, bracketed paste, find and OSC 52 work as
in those tabs, and Casement encodes keys into bytes as it does for SSH.

## Manifest

The manifest's `version` is `2`. A connection type gains `view`:

| Member | Values | Default |
|---|---|---|
| `view` | `display` (pictures, as in version 1) or `terminal` | `display` |

Casement opens a terminal tab for a `terminal` connection. Surfaces,
`key`, `pointer`, `wheel` and `cursor` are not used for it. A Casement that
only knows version 1 lists the plugin as needing a newer Casement.

## Start

Casement offers protocol 2 in `hello`. This plugin speaks only 2: to a
Casement that offers 1 it answers `hello` with `2` (the version-1 rule for
a version it doesn't support) and does nothing else, and Casement reports
that it needs an update.

`connect` gains the terminal's size and type:

```json
{ "type": "connect", "address": "mosh://ann@server.example.com", "username": "ann",
  "password": "", "title": "Server", "cols": 120, "rows": 40, "term": "xterm-256color" }
```

| Member | Meaning |
|---|---|
| `cols`, `rows` | The terminal's size in character cells. This plugin uses 80 x 24 if they are missing. |
| `term` | The terminal type Casement emulates. This plugin's output is always for xterm-256color, which is what libvterm emulates; another value is noted in the log. |

## Messages

| Message | Direction | Meaning |
|---|---|---|
| `{"type":"term_output","data":"<base64>"}` | plugin → Casement | Bytes for the terminal emulator, exactly as an SSH session would deliver them. |
| `{"type":"term_input","data":"<base64>"}` | Casement → plugin | Bytes the terminal produced: typed keys (already encoded, including application cursor mode), pastes (with bracketed-paste markers when the remote side enabled them), mouse reports. |
| `{"type":"term_resize","cols":132,"rows":43}` | Casement → plugin | The terminal's size changed. |
| `state` | plugin → Casement | As in version 1; `connected` once bytes start to flow. |

`data` is standard base64 (RFC 4648, with padding). The bytes are UTF-8
text and escape sequences; a message may end in the middle of a character
or a sequence.

Notes:

- `state connected` may come before the first `term_output`; Casement shows
  the terminal from the first output on, as with SSH. This plugin sends
  `connected` just before its first `term_output`.
- Base64 adds a third to the size. That is fine for terminal traffic.
- There is no flow control. Casement may stop reading the plugin's output
  while its emulator is behind, so a plugin's writes may block; this plugin
  tolerates that (mosh skips intermediate screens anyway).
- The terminal's own copy and paste covers the clipboard; OSC 52 from the
  remote side goes through Casement's normal terminal handling. The
  `clipboard` message is not used.
- The same view type would serve other terminal-like plugins.

## What this plugin sends and expects

1. **Bootstrap.** The plugin runs ssh in a Windows pseudo console of
   `cols` x `rows` and forwards it: `term_output` is the console's output,
   `term_input` goes to its input, and `term_resize` resizes it. So ssh's
   prompts (host key, password, passphrase, two-factor codes) appear and are
   answered in the tab. The `MOSH CONNECT` line mosh-server prints is
   replaced by spaces, and window titles the console host sets are dropped.
2. **Session.** Once ssh has exited, the output is mosh's: on entry the
   alternate screen and application cursor keys, then the screen as mosh
   draws it (its notification bar included), and on exit the sequences that
   switch the modes the session used off again and leave the alternate
   screen. `term_input` bytes are the user's keystrokes for mosh (mosh's
   escape key, Ctrl-^ by default, works as in mosh); `term_resize` resizes
   the remote terminal. Input that arrives while ssh is finishing is kept
   for mosh.
3. **End.**
   - `failed`: ssh could not start mosh-server (the text is the last line
     ssh showed, such as `bash: mosh-server: command not found`), the
     server's address could not be found, or mosh-server never answered over
     UDP (after about 20 seconds, with mosh's "Nothing received from server"
     notice in the meantime).
   - `closed`: mosh-server ended the session (the remote shell exited), or
     the user ended it with Ctrl-^ `.`.
   - On `disconnect` the plugin asks mosh-server to end the session, as
     Ctrl-^ `.` does, waits up to 1.5 seconds for the answer and exits
     without a `state`. A server that doesn't answer in that time (an
     unreachable one, say) keeps the session as it does for a client that
     roamed away: until its shell exits, or until the time set by
     `MOSH_SERVER_NETWORK_TMOUT` on the server.

## Casement's side

A `PluginTermStream` implementing Casement's `TermStream`: `write` sends
`term_input`, `resize` sends `term_resize`, and `term_output` becomes the
stream's output. The plugin host already does the framing and the process
handling.
