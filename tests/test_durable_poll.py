#!/usr/bin/env python3
"""Polling on the shared durable owner, private mock peers only."""
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import threading

from test_telegram_transport import Peer, TOKEN, certificate, peer

BIN = str(pathlib.Path(sys.argv[1]).resolve())


class PollPeer(Peer):
    def __init__(self, *args):
        self.crash_received = threading.Event()
        self.release_crash = threading.Event()
        super().__init__(*args)

    def receipt(self, connection, method, path, body):
        assert method == "POST" and path == f"/bot{TOKEN}/getUpdates"
        p = json.loads(body)
        assert set(p) == {"offset", "limit", "timeout", "allowed_updates"}
        assert p["allowed_updates"] == ["message", "callback_query"]
        assert p["timeout"] == 0 and p["offset"] in (0, 44)
        with self.lock:
            self.calls.append((p["limit"], p["offset"]))
        limit = p["limit"]
        if limit == 9:
            self.crash_received.set()
            assert self.release_crash.wait(10)
            return None
        if limit == 3:
            return 200, [], json.dumps({"description": TOKEN.split(":", 1)[1]}).encode()
        if limit == 4:
            return None
        if limit == 5:
            return 200, [], json.dumps({"ok": True, "result": "x" * 8192}).encode()
        if limit == 6:
            return 429, [], b'{"ok":false,"error_code":429,"parameters":{"retry_after":1}}'
        if limit == 7:
            return 200, [], b'{'
        assert limit == 2
        if p["offset"] == 0:
            return 200, [], b'{"ok":true,"result":[{"update_id":7,"message":{"chat":{"id":42,"type":"private"},"text":"hello"}},{"update_id":43}]}'
        return 200, [], b'{"ok":true,"result":[]}'


def checked(args, env):
    r = subprocess.run(args, env=env, capture_output=True, text=True, timeout=30)
    assert TOKEN.split(":", 1)[1] not in r.stdout + r.stderr, "secret in diagnostics"
    assert r.returncode == 0, (r.returncode, r.stdout, r.stderr)


with tempfile.TemporaryDirectory(prefix="cetta-durable-poll-") as tmp:
    root = pathlib.Path(tmp)
    token = root / "token"
    token.write_text(TOKEN)
    token.chmod(0o600)
    cert, key = certificate(root, "local", "127.0.0.1")
    env = dict(os.environ)
    for i, protocol in enumerate(("http/1.1", "http/1.1", "h2")):
        tls = i > 0
        with peer(protocol, cert if tls else None, key if tls else None, PollPeer) as (server, origin):
            args = [str(token), origin, str(cert) if tls else "-"]
            for mode in ("normal", "pressure"):
                db = root / f"{mode}-{i}.db"
                before = len(server.calls)
                checked([BIN, mode, str(db), *args], env)
                calls = server.calls[before:]
                assert calls[:2] == [(2, 0), (2, 44)], calls
                if mode == "normal":
                    # Host-only getUpdates is deliberately repeat-safe: curl
                    # may resend its lost-response probe on a reused connection.
                    assert 1 <= calls.count((4, 0)) <= 2, calls
                    assert [c for c in calls if c != (4, 0)] == [
                        (2, 0), (2, 44), (3, 0), (5, 0), (6, 0), (7, 0),
                        (2, 0), (2, 0), (2, 0)], calls
                for f in root.glob(f"{mode}-{i}.db*"):
                    assert TOKEN.split(":", 1)[1].encode() not in f.read_bytes(), "secret persisted"
            db = root / f"pending-{i}.db"
            checked([BIN, "pending", str(db), *args], env)
            before = len(server.calls)
            checked([BIN, "recover", str(db), *args], env)
            assert server.calls[before:] == [(2, 44)], "recovery polled before consuming saved response"
            db = root / f"crash-{i}.db"
            proc = subprocess.Popen([BIN, "crash", str(db), *args], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                assert server.crash_received.wait(10)
                proc.kill()
                stdout, stderr = proc.communicate(timeout=5)
                assert TOKEN.split(":", 1)[1] not in stdout + stderr
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait(timeout=5)
                server.release_crash.set()
            before = len(server.calls)
            checked([BIN, "after-crash", str(db), *args], env)
            assert server.calls[before:] == [(2, 0), (2, 44)], "crash advanced an uncommitted offset"
            assert not server.errors, server.errors
            if tls:
                assert server.negotiated and set(server.negotiated) == {protocol}, server.negotiated
            print(f"{'HTTPS' if tls else 'HTTP'} {protocol}: poll/cursor binding, retained responses, privacy, retry deadlines, capacity rollback and restart passed")
