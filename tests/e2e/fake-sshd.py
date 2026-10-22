#!/usr/bin/env python3
"""The other side of tests/e2e/fake-ssh.cpp, for the end-to-end tests.

    fake-sshd.py --port 2222 --dir DIR [--shell SHELL]

Listens on 127.0.0.1 (and ::1 when the system has IPv6) and answers
fake-ssh, one request per connection (a JSON line):

  {"op": "exec", ...}    runs the remote command as sshd would: with
                         /bin/sh -c in a pseudo terminal, SSH_CONNECTION
                         describing this connection, SHELL set to --shell.
                         With "openssh": true it runs the real ssh client
                         with fake-ssh's arguments instead (and -F
                         DIR/ssh_config), which connects back to this port.
  {"op": "config", ...}  answers like `ssh -G`, or runs the real `ssh -G`.
  {"op": "alive", "pid": N}  whether a mosh-server it started still runs.

Answers to exec and config are frames: b"O" + 4-byte big-endian length +
output, then b"X" + the exit status as 4 bytes.

An exec request with "delay_ms": N puts a UDP relay between the plugin and
mosh-server that delays every datagram N ms each way: it changes the port
in the MOSH CONNECT line to its own (61050-61099). {"op": "pause", "pid":
P, "seconds": S} makes the relay for mosh-server P drop everything for S
seconds, like a dead link.

A connection that starts with an SSH identification string ("SSH-") goes to
the real sshd in inetd mode (sshd -i -f DIR/sshd_config), so the real ssh
client and sshd talk over this one port.

It records the mosh-servers it starts ("[mosh-server detached, pid = N]")
and stops them, and only them, when it gets SIGTERM.
"""

import argparse
import errno
import fcntl
import json
import os
import pty
import re
import select
import signal
import socket
import struct
import subprocess
import sys
import termios
import threading
import time

DETACHED = re.compile(rb"\[mosh-server detached, pid = (\d+)\]")
CONNECT = re.compile(rb"MOSH CONNECT (\d+) ([A-Za-z0-9/+]{22})")
RELAY_PORTS = range(61050, 61100)

lock = threading.Lock()
mosh_servers = set()
children = set()
relays = {}  # mosh-server pid -> Relay


class Relay:
    """Passes UDP datagrams between the plugin and mosh-server, late."""

    def __init__(self, server_host, server_port, delay):
        family = socket.AF_INET6 if ":" in server_host else socket.AF_INET
        self.sock = socket.socket(family, socket.SOCK_DGRAM)
        for port in RELAY_PORTS:
            try:
                self.sock.bind((server_host, port))
                break
            except OSError:
                continue
        else:
            raise OSError("no free relay port")
        self.port = self.sock.getsockname()[1]
        self.server = (server_host, server_port)
        self.delay = delay / 1000.0
        self.paused_until = 0.0
        self.client = None
        self.queue = []
        self.started = time.time()
        threading.Thread(target=self.run, daemon=True).start()

    def run(self):
        last = time.time()
        while time.time() - last < 300:  # until mosh-server has been quiet for 5 minutes
            now = time.time()
            timeout = max(0.0, min([d for d, _, _ in self.queue] + [now + 0.5]) - now)
            ready, _, _ = select.select([self.sock], [], [], timeout)
            now = time.time()
            if ready:
                data, addr = self.sock.recvfrom(65536)
                last = now
                if now >= self.paused_until:
                    if addr[:2] == self.server[:2]:
                        if self.client:
                            self.queue.append((now + self.delay, self.client, data))
                    else:
                        self.client = addr
                        self.queue.append((now + self.delay, self.server, data))
            due = [q for q in self.queue if q[0] <= now]
            self.queue = [q for q in self.queue if q[0] > now]
            for _, dest, data in sorted(due, key=lambda q: q[0]):
                try:
                    self.sock.sendto(data, dest)
                except OSError:
                    pass
        self.sock.close()


def log(text):
    print("fake-sshd: " + text, file=sys.stderr, flush=True)


def frame(kind, payload=b""):
    if kind == b"X":
        return kind + struct.pack(">i", payload)
    return kind + struct.pack(">I", len(payload)) + payload


def run_in_pty(conn, argv, env, delay=None):
    """Runs argv with a new pseudo terminal as its controlling terminal and
    sends what it writes to conn. Returns the exit status. With `delay`, the
    MOSH CONNECT line names a relay instead (see Relay)."""
    master, slave = pty.openpty()
    fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
    # setsid --ctty: a session of its own with the terminal as its
    # controlling terminal, as sshd sets up a session.
    proc = subprocess.Popen(["setsid", "--ctty", "--wait"] + argv, stdin=slave, stdout=slave, stderr=slave,
                            env=env, close_fds=True)
    os.close(slave)
    with lock:
        children.add(proc.pid)
    seen = b""
    pending = b""  # with a relay: output held until the end of its line
    relay = None
    try:
        while True:
            try:
                data = os.read(master, 65536)
            except OSError as e:
                if e.errno == errno.EIO:  # every writer has gone
                    break
                raise
            if not data:
                break
            if delay is not None:
                pending += data
                cut = pending.rfind(b"\n") + 1
                data, pending = pending[:cut], pending[cut:]
                m = CONNECT.search(data)
                if m and relay is None:
                    relay = Relay(env["SSH_CONNECTION"].split()[2], int(m.group(1)), delay)
                    data = data[:m.start(1)] + str(relay.port).encode() + data[m.end(1):]
            seen = (seen + data)[-4096:]
            for m in DETACHED.finditer(seen):
                with lock:
                    mosh_servers.add(int(m.group(1)))
                    if relay:
                        relays[int(m.group(1))] = relay
            if data:
                conn.sendall(frame(b"O", data))
        if pending:
            conn.sendall(frame(b"O", pending))
    finally:
        os.close(master)
    status = proc.wait()
    with lock:
        children.discard(proc.pid)
    return status if status >= 0 else 255


def exec_request(conn, req, args, peer, local):
    if req.get("openssh"):
        env = dict(os.environ)
        argv = ["ssh", "-F", os.path.join(args.dir, "ssh_config")] + req["args"]
    else:
        env = {
            "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
            "HOME": os.path.join(args.dir, "home"),
            "USER": os.environ.get("USER", "root"),
            "LOGNAME": os.environ.get("USER", "root"),
            "LANG": "C.UTF-8",
            "TERM": "xterm-256color",
            "SHELL": args.shell,
            "SSH_CONNECTION": "%s %d %s %d" % (peer[0], peer[1], local[0], local[1]),
        }
        argv = ["/bin/sh", "-c", req["command"]]
    log("exec " + " ".join(argv))
    return run_in_pty(conn, argv, env, req.get("delay_ms"))


def config_request(conn, req, args):
    if req.get("openssh"):
        out = subprocess.run(["ssh", "-F", os.path.join(args.dir, "ssh_config")] + req["args"],
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        conn.sendall(frame(b"O", out.stdout))
        return out.returncode
    text = "user %s\nhostname %s\nport %s\naddressfamily %s\nproxycommand none\nproxyjump none\n" % (
        req.get("user") or os.environ.get("USER", "root"), req["host"], req["port"], req["address_family"])
    conn.sendall(frame(b"O", text.encode()))
    return 0


def alive(pid):
    with lock:
        if pid not in mosh_servers:
            return False
    try:
        with open("/proc/%d/stat" % pid) as f:
            return f.read().split(") ")[-1][0] != "Z"
    except OSError:
        return False


def serve(conn, peer, args):
    try:
        local = conn.getsockname()
        # OpenSSH's client sends its identification string first.
        conn.settimeout(5.0)
        try:
            first = conn.recv(4, socket.MSG_PEEK)
        except socket.timeout:
            return
        conn.settimeout(None)
        if first.startswith(b"SSH-"):
            sshd = subprocess.Popen(["/usr/sbin/sshd", "-i", "-f", os.path.join(args.dir, "sshd_config"),
                                     "-E", os.path.join(args.dir, "sshd.log")],
                                    stdin=conn.fileno(), stdout=conn.fileno(), close_fds=True)
            conn.close()
            sshd.wait()
            return
        line = b""
        while not line.endswith(b"\n"):
            data = conn.recv(4096)
            if not data:
                return
            line += data
        req = json.loads(line)
        op = req.get("op")
        if op == "alive":
            conn.sendall((json.dumps({"alive": alive(int(req["pid"]))}) + "\n").encode())
            return
        if op == "pause":
            with lock:
                relay = relays.get(int(req["pid"]))
            if relay:
                relay.paused_until = time.time() + float(req["seconds"])
            conn.sendall((json.dumps({"paused": relay is not None}) + "\n").encode())
            return
        if op == "exec":
            status = exec_request(conn, req, args, peer, local)
        elif op == "config":
            status = config_request(conn, req, args)
        else:
            status = 255
        conn.sendall(frame(b"X", status))
    except (BrokenPipeError, ConnectionResetError):
        pass
    except Exception as e:  # keep serving the other tests
        log("error: %r" % e)
    finally:
        try:
            conn.close()
        except OSError:
            pass


def stop(signum, _frame):
    with lock:
        pids = list(mosh_servers) + list(children)
    for pid in pids:
        try:
            os.kill(pid, signal.SIGTERM)
        except OSError:
            pass
    log("stopped %d processes it started" % len(pids))
    os._exit(0)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--port", type=int, default=2222)
    p.add_argument("--dir", required=True)
    p.add_argument("--shell", default="/bin/sh")
    args = p.parse_args()
    os.makedirs(os.path.join(args.dir, "home"), exist_ok=True)
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)

    listeners = []
    for family, host in ((socket.AF_INET, "127.0.0.1"), (socket.AF_INET6, "::1")):
        try:
            s = socket.socket(family, socket.SOCK_STREAM)
        except OSError:
            continue  # no IPv6 here
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        if family == socket.AF_INET6:
            s.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)
        try:
            s.bind((host, args.port))
        except OSError as e:
            if family == socket.AF_INET:
                raise
            log("no IPv6 listener: %s" % e)
            continue
        s.listen(16)
        listeners.append(s)
    log("listening on port %d (%s)" % (args.port, ", ".join(l.getsockname()[0] for l in listeners)))
    while True:
        ready, _, _ = select.select(listeners, [], [])
        for s in ready:
            conn, peer = s.accept()
            threading.Thread(target=serve, args=(conn, peer, args), daemon=True).start()


if __name__ == "__main__":
    main()
