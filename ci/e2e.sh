#!/usr/bin/env bash
# Runs the end-to-end tests of a MinGW build under Wine, against a real
# mosh-server:
#
#   ci/e2e.sh <build dir>
#
# It starts tests/e2e/fake-sshd.py, the remote side for fake-ssh (the
# tests' stand-in for ssh.exe, which doesn't run under Wine), and, when
# OpenSSH's ssh and sshd are installed, sets them up for the test that
# runs the real ones: a key, a host key, and configurations in a
# temporary directory. Needs mosh-server, python3, setsid and Wine.
#
# It uses TCP port 2222 and UDP ports 61000-61099 only, and stops only
# the processes it started.
set -euo pipefail

BUILD=$(realpath "$1")
REPO=$(cd "$(dirname "$0")/.." && pwd)
PORT=2222
WORK=$(mktemp -d)
HELPER_PID=

cleanup() {
    if [ -n "$HELPER_PID" ]; then
        kill "$HELPER_PID" 2>/dev/null || true
        wait "$HELPER_PID" 2>/dev/null || true
    fi
    if [ -s "$WORK/sshd.log" ]; then
        echo "--- sshd log"
        cat "$WORK/sshd.log"
    fi
    rm -rf "$WORK"
}
trap cleanup EXIT

command -v mosh-server >/dev/null || { echo "mosh-server isn't installed" >&2; exit 1; }

if command -v ssh >/dev/null && [ -x /usr/sbin/sshd ]; then
    ssh-keygen -q -t ed25519 -N '' -C mosh-e2e -f "$WORK/id_ed25519"
    ssh-keygen -q -t ed25519 -N '' -C mosh-e2e-host -f "$WORK/ssh_host_ed25519_key"
    cp "$WORK/id_ed25519.pub" "$WORK/authorized_keys"
    cat > "$WORK/sshd_config" <<END
HostKey $WORK/ssh_host_ed25519_key
AuthorizedKeysFile $WORK/authorized_keys
PidFile none
UsePAM no
StrictModes no
PermitRootLogin prohibit-password
PasswordAuthentication no
KbdInteractiveAuthentication no
SetEnv LANG=C.UTF-8 SHELL=$REPO/tests/e2e/shell.sh
END
    cat > "$WORK/ssh_config" <<END
Host mosh-e2e
    HostName 127.0.0.1
    Port $PORT
Host *
    IdentityFile $WORK/id_ed25519
    IdentitiesOnly yes
    StrictHostKeyChecking no
    UserKnownHostsFile /dev/null
    BatchMode yes
    LogLevel ERROR
END
    # sshd started as root wants its privilege separation directory.
    if [ "$(id -u)" = 0 ]; then mkdir -p /run/sshd; fi
    export CASEMENT_MOSH_E2E_OPENSSH_USER=$(id -un)
else
    echo "no ssh/sshd: the real-OpenSSH test is skipped" >&2
fi

python3 "$REPO/tests/e2e/fake-sshd.py" --port "$PORT" --dir "$WORK" --shell "$REPO/tests/e2e/shell.sh" \
    2> >(tee "$WORK/fake-sshd.log" >&2) &
HELPER_PID=$!
for _ in $(seq 100); do
    grep -q "listening" "$WORK/fake-sshd.log" 2>/dev/null && break
    kill -0 "$HELPER_PID" 2>/dev/null || { echo "fake-sshd.py didn't start" >&2; exit 1; }
    sleep 0.1
done

export CASEMENT_MOSH_E2E_HELPER=127.0.0.1:$PORT
export WINEDEBUG=${WINEDEBUG:--all}
ctest --test-dir "$BUILD" --output-on-failure -L e2e "${@:2}"
