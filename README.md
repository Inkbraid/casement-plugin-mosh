# Mosh support for Casement

A Casement protocol plugin that opens `mosh://` connections in a terminal
tab: it starts `mosh-server` on the remote host through ssh and runs a
native Windows port of the mosh client, so the session survives network
changes, sleep and slow links.

Licensed under the GNU General Public License, version 3 or later, with
the OpenSSL linking exception that mosh uses (see [LICENSE](LICENSE)).
