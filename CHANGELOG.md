# Changelog

## 0.1.0 (2026-10-23)

- `mosh://` connections in Casement's terminal view: plugin protocol
  version 2 with proposal P7, written up in `docs/terminal-view.md`. The
  plugin speaks protocol 2 only.
- Bootstrap through Windows' `ssh.exe` in a pseudo console of the tab's
  size: ssh's prompts in the tab, the `MOSH CONNECT` line kept off the
  screen, and the server's address as `ssh -G` and ssh's resolver see it.
- The mosh 1.4.0 client, ported to Windows: SSP over UDP with AES-128-OCB
  (OpenSSL), roaming, the notification bar, output for xterm-256color,
  and local echo (adaptive, always, never).
- UDP port or range, mosh-server command and extra ssh options (jump
  hosts); IPv6.
- *closed* when the shell exits or on Ctrl-^ `.`; *failed* with ssh's last
  line, or when mosh-server doesn't answer over UDP; closing the tab ends
  the session on the server.
- Extra ssh options that make ssh run programs, load libraries or write
  files on this computer are refused, and so are host and user names with
  shell metacharacters.
- Unit tests; end-to-end tests that run the plugin under Wine against a
  real mosh-server and OpenSSH; CI for the MSVC and MinGW builds; a
  release workflow that also publishes the sources of the libraries in the
  exe.
