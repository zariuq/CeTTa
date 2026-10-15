#!/usr/bin/env python3
"""Continuous native service, independent worker reconnects, private Bot API."""
import collections
import json
import os
import pathlib
import signal
import socket
import sqlite3
import subprocess
import sys
import tempfile
import threading
import time

from test_telegram_transport import Peer, TOKEN, certificate, peer

BIN = str(pathlib.Path(sys.argv[1]).resolve())


class ServicePeer(Peer):
    def __init__(self, *args):
        self.crash_received = threading.Event()
        self.release_crash = threading.Event()
        self.poll_waiting = threading.Event()
        self.release_poll = threading.Event()
        self.delay_poll = False
        self.hold = False
        self.last_update = 41
        self.rated = False
        super().__init__(*args)

    def receipt(self, connection, method, path, body):
        assert method == "POST"
        data = json.loads(body)
        methods = {f"/bot{TOKEN}/sendMessage": (1, {"chat_id": 42, "message_thread_id": 17,
            "reply_parameters": {"message_id": 3}, "text": "fixture-one"}),
            f"/bot{TOKEN}/editMessageText": (2, {"chat_id": 42, "message_id": 9, "text": "fixture-two", "parse_mode": "HTML"}),
            f"/bot{TOKEN}/deleteMessage": (3, {"chat_id": 42, "message_id": 9})}
        if path in methods:
            item, expected = methods[path]
            assert data == expected
            with self.lock:
                self.calls.append(("send", item))
            if item == 3:
                self.crash_received.set()
                assert self.release_crash.wait(10)
                return None
            if item == 2:
                return None
            return 200, [], b'{"ok":true,"result":{"message_id":1}}'
        assert path == f"/bot{TOKEN}/getUpdates"
        with self.lock:
            self.calls.append(("poll", data["offset"]))
            rate = not self.rated
            self.rated = True
            hold, delay, latest = self.hold, self.delay_poll, self.last_update
        if rate:
            return 429, [], b'{"ok":false,"error_code":429,"parameters":{"retry_after":1}}'
        if delay:
            self.poll_waiting.set()
            assert self.release_poll.wait(10)
        if hold:
            return 200, [], b'{'
        result = [] if data["offset"] > latest else [
            {"update_id": latest, "message": {"chat": {"id": 42, "type": "private"}, "text": "fixture input"}}]
        return 200, [], json.dumps({"ok": True, "result": result}).encode()


def records(db, space):
    try:
        with sqlite3.connect(f"file:{db}?mode=ro", uri=True) as conn:
            return dict(conn.execute("SELECT key,value FROM records WHERE space=?", (space,)))
    except sqlite3.OperationalError:
        return {}


def wait_for(proc, predicate, description, seconds=12):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if predicate():
            return
        if proc.poll() is not None:
            out, err = proc.communicate()
            assert TOKEN.split(":", 1)[1] not in out + err
            raise AssertionError((description, proc.returncode, out, err))
        time.sleep(0.025)
    raise AssertionError(description)


def rpc(sock, code, task="", body=b""):
    name = task.encode()
    sock.sendall(b"CWP1" + bytes([code, len(name)]) + name + body)
    reply = sock.recv(65606)
    assert reply[:4] == b"CWP1" and len(reply) >= 6
    n = reply[5]
    return reply[4], reply[6:6+n].decode(), reply[6+n:]


with tempfile.TemporaryDirectory(prefix="cetta-service-") as temp:
    root = pathlib.Path(temp)
    token = root / "token"
    token.write_text(TOKEN)
    token.chmod(0o600)
    cert, key = certificate(root, "local", "127.0.0.1")
    env = dict(os.environ)
    for name in ("http_proxy", "https_proxy", "all_proxy", "HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY"):
        env[name] = "http://127.0.0.1:1"
    env["NO_PROXY"] = env["no_proxy"] = ""
    for i, protocol in enumerate(("http/1.1", "http/1.1", "h2")):
        tls = i > 0
        with peer(protocol, cert if tls else None, key if tls else None, ServicePeer) as (server, origin):
            db, path = root / f"state-{i}.db", root / f"worker-{i}.sock"
            listener = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
            listener.bind(str(path)); listener.listen(16)
            processes = []

            def start(mode="run"):
                p = subprocess.Popen([BIN, mode, str(db), str(token), origin, str(cert) if tls else "-", str(listener.fileno())],
                    pass_fds=(listener.fileno(),), env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                processes.append(p)
                return p

            def stop(p, kill=False):
                p.send_signal(signal.SIGKILL if kill else signal.SIGTERM)
                out, err = p.communicate(timeout=8)
                assert TOKEN.split(":", 1)[1] not in out + err
                assert p.returncode == (-signal.SIGKILL if kill else 0), (p.returncode, out, err)
                return out

            def client():
                s = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
                s.settimeout(5); s.connect(str(path)); return s

            try:
                q = subprocess.run([BIN, "quota", str(root / f"quota-{i}.db"), str(token), origin,
                                    str(cert) if tls else "-", str(listener.fileno())],
                    pass_fds=(listener.fileno(),), env=env, capture_output=True, text=True, timeout=10)
                assert q.returncode == 0, (q.returncode, q.stdout, q.stderr)
                assert TOKEN.split(":", 1)[1] not in q.stdout + q.stderr
                p = start()
                # No cognitive worker exists while retry, receive, sends and timer progress.
                wait_for(p, lambda: len(records(db, "host.received")) == 1 and
                    len(records(db, "host.outcomes")) == 3, "transport/timer stalled without worker")
                assert any(b"uncertain" in v for v in records(db, "host.outcomes").values())
                a = client(); code, task, body = rpc(a, 1)
                assert code == 65 and body == b"service observation"; a.close()
                with server.lock:
                    server.last_update = 43
                wait_for(p, lambda: len(records(db, "host.received")) == 2, "worker disconnection stopped receive")
                # Force TERM while getUpdates is in flight; next owner must retry it.
                with server.lock:
                    server.delay_poll = True
                wait_for(p, server.poll_waiting.is_set, "poll never entered shutdown window")
                stop(p)
                assert any(b"shutdown" in v for v in records(db, "host.polls").values())
                with server.lock:
                    server.delay_poll = False
                    server.last_update = 45
                server.release_poll.set()
                p = start()
                wait_for(p, lambda: len(records(db, "host.received")) == 3, "graceful restart left poller held")
                a = client(); assert rpc(a, 1) == (65, task, body)
                assert rpc(a, 2, task, b"recorded response")[0] == 66; a.close()
                wait_for(p, lambda: len(records(db, "host.worker-results")) == 1, "worker result not recorded")
                stop(p, kill=True)
                p = start("crash-send")
                wait_for(p, server.crash_received.is_set, "crash send not received")
                stop(p, kill=True); server.release_crash.set()
                p = start()
                wait_for(p, lambda: len(records(db, "host.outcomes")) == 4, "crash claim did not recover")
                assert sum(b"uncertain" in v for v in records(db, "host.outcomes").values()) == 2
                a = client(); assert rpc(a, 3, task)[0] == 66
                before = len(records(db, "host.inbox"))
                assert rpc(a, 2, task, b"recorded response")[0] == 66
                assert len(records(db, "host.inbox")) == before
                # A held provider response must not repeatedly evaluate or stop IPC.
                with server.lock:
                    server.hold = True
                wait_for(p, lambda: any(b"malformed-json" in v for v in records(db, "host.poll-control").values()), "malformed poll not held")
                with server.lock:
                    calls = len(server.calls)
                time.sleep(0.35)
                assert rpc(a, 3, task)[0] == 66; a.close()
                with server.lock:
                    assert len(server.calls) == calls, "held source was repolled"
                summary = stop(p)
                assert "host_faults=1" in summary and "http_faults=0" in summary, summary
                counts = collections.Counter(n for kind, n in server.calls if kind == "send")
                assert counts == {1: 1, 2: 1, 3: 1}, counts
                assert not server.errors, server.errors
                if tls:
                    assert server.negotiated and set(server.negotiated) == {protocol}
                print(f"{'HTTPS' if tls else 'HTTP'} {protocol}: independent worker, continuous receive/timers, graceful poll restart, uncertain sends and held-source IPC passed")
                # An incomplete policy must not be committed or reevaluated in
                # a hot loop. IPC remains available while this source is parked.
                db = root / f"fuel-{i}.db"
                p = start("fuel")
                wait_for(p, lambda: len(records(db, "host.polls")) == 1 and
                    len(records(db, "host.outcomes")) == 3, "fuel probe did not settle")
                time.sleep(0.2)
                assert not records(db, "host.received") and not records(db, "host.cursors")
                a = client(); assert rpc(a, 1)[0] == 65; a.close()
                summary = stop(p)
                assert "host_faults=1" in summary and "http_faults=0" in summary, summary
            finally:
                server.release_crash.set(); server.release_poll.set()
                for p in processes:
                    if p.poll() is None:
                        p.kill(); p.communicate(timeout=8)
                listener.close()
