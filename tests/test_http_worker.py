#!/usr/bin/env python3
"""Private loopback fixture and journal; no production credentials or endpoints."""
import pathlib
import selectors
import subprocess
import sys
import tempfile
import collections
import http.server
import socket
import threading

fixture, test = (str(pathlib.Path(x).resolve()) for x in sys.argv[1:])
with tempfile.TemporaryDirectory(prefix="cetta-http-worker-") as directory:
    server = subprocess.Popen([fixture], stdout=subprocess.PIPE, text=True)
    try:
        with selectors.DefaultSelector() as ready:
            ready.register(server.stdout, selectors.EVENT_READ)
            assert ready.select(timeout=5), "fixture did not start"
        port = int(server.stdout.readline().strip())
        subprocess.run([test, f"http://127.0.0.1:{port}", str(pathlib.Path(directory) / "journal.db")],
                       check=True, timeout=40)
    finally:
        server.terminate()
        server.wait(timeout=5)


class KeepAliveFixture(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self):
        super().__init__(("127.0.0.1", 0), KeepAliveHandler)
        self.lock = threading.Lock()
        self.connections = 0
        self.receipts = []


class KeepAliveHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def setup(self):
        super().setup()
        with self.server.lock:
            self.server.connections += 1
            self.connection_id = self.server.connections

    def log_message(self, *args):
        pass

    def do_GET(self):
        payload = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        with self.server.lock:
            occurrence = 1 + sum(r[0] == self.path for r in self.server.receipts)
            self.server.receipts.append((self.path, self.connection_id, self.command, payload))
        if self.path == "/drop-once" and occurrence == 1:
            # Accepted in full; lose every byte of the response. On a reused
            # connection curl retries this, even for POST, without host involvement.
            self.close_connection = True
            self.connection.shutdown(socket.SHUT_RDWR)
            return
        self.send_response(200)
        self.send_header("Content-Length", "2")
        self.end_headers()
        self.wfile.write(b"ok")
        self.wfile.flush()

    do_POST = do_GET


for method in ("POST", "GET"):
    for policy in ("default", "idempotent"):
        with KeepAliveFixture() as server:
            owner = threading.Thread(target=server.serve_forever, daemon=True)
            owner.start()
            try:
                run = subprocess.run(
                    [test, "--connection-policy", f"http://127.0.0.1:{server.server_port}", policy, method],
                    capture_output=True, text=True, timeout=15)
                with server.lock:
                    receipts = list(server.receipts)
                counts = collections.Counter(r[0] for r in receipts)
                assert counts == {"/isolated-success": 1, "/isolated-after": 1, "/warm": 1, "/success": 1,
                                  "/drop-once": 2 if policy == "idempotent" else 1, "/after": 1}, receipts
                assert all(r[2] == method and r[3] == (r[0].encode() if method == "POST" else b"") for r in receipts), receipts
                conn = {r[0]: r[1] for r in receipts}
                if policy == "default":
                    assert conn["/isolated-success"] != conn["/isolated-after"], receipts
                    assert conn["/isolated-after"] == conn["/warm"], receipts
                    assert conn["/warm"] == conn["/after"], receipts
                    assert len({conn["/warm"], conn["/success"], conn["/drop-once"]}) == 3, receipts
                else:
                    assert conn["/isolated-success"] == conn["/isolated-after"], receipts
                    assert conn["/warm"] == conn["/success"], receipts
                    assert conn["/warm"] != conn["/after"], receipts
                assert run.returncode == 0, (run.stdout, run.stderr, receipts)
                print(f"Keep-alive {method} {policy}: remote counts and connection isolation passed")
            finally:
                server.shutdown()
                owner.join(timeout=5)
