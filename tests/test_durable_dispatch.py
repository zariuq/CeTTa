#!/usr/bin/env python3
"""Durable rho-to-Telegram path on private HTTP/TLS/h2 peers; no live traffic."""
import collections
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import threading
import time

from test_telegram_transport import Peer, TOKEN, certificate, peer

BIN = str(pathlib.Path(sys.argv[1]).resolve())


class DurablePeer(Peer):
    def __init__(self, *args):
        self.received_crash = threading.Event()
        self.release_crash = threading.Event()
        self.polls = []
        self.effect_during_poll = threading.Event()
        super().__init__(*args)

    def receipt(self, connection, method, path, body):
        parsed = json.loads(body)
        if path == f"/bot{TOKEN}/getUpdates":
            assert method == "POST" and parsed == {"offset": 0, "limit": 1, "timeout": 0, "allowed_updates": []}
            self.polls.append(parsed)
            assert self.effect_during_poll.wait(5), "long poll blocked outgoing effect"
            return 200, [], b'{"ok":true,"result":[]}'
        assert parsed["chat_id"] == "approved-chat"
        assert method == "POST" and path == f"/bot{TOKEN}/sendMessage"
        item = parsed["request"]
        if item == 1:
            self.effect_during_poll.set()
        with self.lock:
            self.calls.append((item, connection))
        if item == 2:
            return None
        if item == 3:
            return 200, [], json.dumps({"description": TOKEN.split(":", 1)[1]}).encode()
        if item == 7:
            self.received_crash.set()
            assert self.release_crash.wait(10)
            return None
        if item == 8:
            return 200, [], json.dumps({"ok": True, "result": "x" * 8192}).encode()
        if item == 10:
            time.sleep(0.3)
        return 200, [], b'{"ok":true,"result":{"message_id":1}}'


def checked(result):
    assert TOKEN.split(":", 1)[1] not in result.stdout + result.stderr, "credential in diagnostics"
    assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)


with tempfile.TemporaryDirectory(prefix="cetta-durable-dispatch-") as tmp:
    root = pathlib.Path(tmp)
    token = root / "token"
    token.write_text(TOKEN)
    token.chmod(0o600)
    cert, key = certificate(root, "local", "127.0.0.1")
    env = dict(os.environ)
    for k in ("http_proxy", "https_proxy", "all_proxy", "HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY"):
        env[k] = "http://127.0.0.1:1"
    env["NO_PROXY"] = env["no_proxy"] = ""
    for i, protocol in enumerate(("http/1.1", "http/1.1", "h2")):
        tls = i > 0
        with peer(protocol, cert if tls else None, key if tls else None, DurablePeer) as (server, origin):
            db = root / f"normal-{i}.db"
            args = [str(token), origin, str(cert) if tls else "-"]
            checked(subprocess.run([BIN, "normal", str(db), *args], env=env, capture_output=True, text=True, timeout=40))
            assert len(server.polls) == 1
            counts = collections.Counter(x[0] for x in server.calls)
            for item in (1, 2, 3, 8, 9):
                assert counts[item] == 1, counts
            assert set(counts) <= {1, 2, 3, 8, 9, 10} and counts[10] <= 1, counts
            # No secret reaches the authoritative journal, including WAL bytes.
            for file in root.glob(f"normal-{i}.db*"):
                assert TOKEN.split(":", 1)[1].encode() not in file.read_bytes(), "secret persisted"
            db = root / f"crash-{i}.db"
            proc = subprocess.Popen([BIN, "crash", str(db), *args], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                assert server.received_crash.wait(10), "request did not reach mock"
                proc.kill()
                stdout, stderr = proc.communicate(timeout=5)
                assert TOKEN.split(":", 1)[1] not in stdout + stderr
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait(timeout=5)
                server.release_crash.set()
            checked(subprocess.run([BIN, "recover", str(db), *args], env=env, capture_output=True, text=True, timeout=20))
            assert collections.Counter(x[0] for x in server.calls)[7] == 1, "recovery resent HTTP"
            db = root / f"pressure-{i}.db"
            checked(subprocess.run([BIN, "pressure", str(db), *args], env=env, capture_output=True, text=True, timeout=30))
            checked(subprocess.run([BIN, "recover", str(db), *args], env=env, capture_output=True, text=True, timeout=20))
            assert collections.Counter(x[0] for x in server.calls)[12] == 1, "recording failure resent HTTP"
            assert not server.errors, server.errors
            if tls:
                assert server.negotiated and set(server.negotiated) == {protocol}, server.negotiated
            print(f"{'HTTPS' if tls else 'HTTP'} {protocol}: durable rho round trip, explicit uncertainty, privacy, cancellation, minimal recording and SIGKILL recovery passed")
