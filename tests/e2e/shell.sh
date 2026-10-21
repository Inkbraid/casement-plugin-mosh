#!/bin/sh
# The login shell mosh-server starts in the end-to-end tests: a plain sh
# with a prompt the tests can wait for.
PS1='e2e$ '
export PS1
unset ENV
exec /bin/sh -i
