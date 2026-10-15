#!/usr/bin/env python3
"""Private fake Bot API: HTTP/1.1, TLS, and HTTP/2 drop-after-read faults.

Requires openssl and Python h2. No real credentials or Telegram traffic.
"""
import collections
import contextlib
import errno
import http.server
import json
import os
import pathlib
import socket
import socketserver
import ssl
import subprocess
import sys
import tempfile
import threading

from h2.config import H2Configuration
from h2.connection import H2Connection
from h2.events import DataReceived, RequestReceived, StreamEnded


TOKEN = "123456789:TEST_ONLY_abcdefghijklmnopqrstuvwxyz0123456789"
BIN = str(pathlib.Path(sys.argv[1]).resolve())


class Peer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, protocol, tls):
        self.protocol, self.tls = protocol, tls
        self.lock = threading.Lock()
        self.calls, self.errors, self.negotiated = [], [], []
        self.next_connection = 0
        super().__init__(("127.0.0.1", 0), Handler)

    def receipt(self, connection, method, path, body):
        # Receipt is counted after the complete body arrives, before a reply.
        with self.lock:
            item = json.loads(body)["request"]
            self.calls.append((item, connection, method, path))
        expected = "getUpdates" if item in (4, 5) else "sendMessage"
        assert method == "POST" and path == f"/bot{TOKEN}/{expected}"
        if item == 2:
            return None  # The effect happened; no response survives.
        if item == 3:
            return 200, [], json.dumps({"ok": False, "description": TOKEN.split(":", 1)[1]}).encode()
        if item == 6:
            return 307, [("location", f"http://127.0.0.1:{self.server_address[1]}/redirected")], b""
        return 200, [], b'{"ok":true,"result":[]}'


class HTTP1(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass  # Do not log the credential-bearing URL, even in the mock.

    def do_POST(self):
        body = self.rfile.read(int(self.headers["Content-Length"]))
        assert self.headers["Content-Type"] == "application/json"
        result = self.server.receipt(self.connection_id, "POST", self.path, body)
        if result is None:
            self.close_connection = True
            self.connection.shutdown(socket.SHUT_RDWR)
            return
        code, headers, response = result
        self.send_response(code)
        for key, value in headers:
            self.send_header(key, value)
        self.send_header("Content-Length", str(len(response)))
        self.end_headers()
        self.wfile.write(response)


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        with self.server.lock:
            self.server.next_connection += 1
            connection = self.server.next_connection
        sock = self.request
        sock.settimeout(5)
        try:
            if self.server.tls:
                try:
                    sock = self.server.tls.wrap_socket(sock, server_side=True)
                except ssl.SSLError:
                    return  # Expected untrusted-CA/hostname tests.
                selected = sock.selected_alpn_protocol()
                with self.server.lock:
                    self.server.negotiated.append(selected)
                assert selected == self.server.protocol
            if self.server.protocol != "h2":
                class BoundHTTP1(HTTP1):
                    connection_id = connection
                BoundHTTP1(sock, self.client_address, self.server)
                return
            h2 = H2Connection(config=H2Configuration(client_side=False, header_encoding="utf-8"))
            h2.initiate_connection()
            sock.sendall(h2.data_to_send())
            streams = {}
            while data := sock.recv(65536):
                for event in h2.receive_data(data):
                    if isinstance(event, RequestReceived):
                        streams[event.stream_id] = [dict(event.headers), bytearray()]
                    elif isinstance(event, DataReceived):
                        streams[event.stream_id][1].extend(event.data)
                        h2.acknowledge_received_data(event.flow_controlled_length, event.stream_id)
                    elif isinstance(event, StreamEnded):
                        headers, body = streams.pop(event.stream_id)
                        assert headers["content-type"] == "application/json"
                        result = self.server.receipt(connection, headers[":method"], headers[":path"], body)
                        if result is None:
                            sock.shutdown(socket.SHUT_RDWR)
                            return
                        code, extra, response = result
                        h2.send_headers(event.stream_id,
                                        [(":status", str(code)), ("content-length", str(len(response)))] + extra,
                                        end_stream=not response)
                        if response:
                            h2.send_data(event.stream_id, response, end_stream=True)
                pending = h2.data_to_send()
                if pending:
                    sock.sendall(pending)
        except (ConnectionResetError, BrokenPipeError):
            pass  # FRESH_CONNECT/FORBID_REUSE closes completed connections.
        except OSError as exc:
            # A deliberately killed client may already have disconnected when
            # the mock drops its response and shuts down the TLS socket.
            if exc.errno != errno.ENOTCONN:
                with self.server.lock:
                    self.server.errors.append((type(exc).__name__, exc.errno))
        except Exception as exc:
            with self.server.lock:
                self.server.errors.append(type(exc).__name__)
        finally:
            sock.close()


@contextlib.contextmanager
def peer(protocol, cert=None, key=None, peer_type=Peer):
    tls = None
    if cert:
        tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls.load_cert_chain(cert, key)
        tls.set_alpn_protocols([protocol])
    server = peer_type(protocol, tls)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        yield server, f"{'https' if tls else 'http'}://127.0.0.1:{server.server_address[1]}"
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)


def certificate(directory, name, address):
    cert, key = directory / f"{name}.crt", directory / f"{name}.key"
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                    "-keyout", str(key), "-out", str(cert), "-days", "1",
                    "-subj", "/CN=fixture", "-addext", f"subjectAltName=IP:{address}"],
                   capture_output=True, check=True)
    return cert, key


def run(*args):
    env = dict(os.environ)
    # Any accidental use of ambient proxy settings prevents reaching the mock.
    for key in ("http_proxy", "https_proxy", "all_proxy", "HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY"):
        env[key] = "http://127.0.0.1:1"
    env["NO_PROXY"] = env["no_proxy"] = ""
    result = subprocess.run([BIN, *map(str, args)], capture_output=True, text=True, timeout=30, env=env)
    assert TOKEN.split(":", 1)[1] not in result.stdout + result.stderr, "credential leaked into diagnostics"
    assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
    return result.stdout.strip()


def main():
    with tempfile.TemporaryDirectory(prefix="cetta-telegram-") as tmp:
        directory = pathlib.Path(tmp)
        token_file = directory / "token"
        print(run(token_file))
        cert, key = certificate(directory, "local", "127.0.0.1")
        wrong, wrong_key = certificate(directory, "wrong-host", "127.0.0.2")
        for label, protocol, tls in (("HTTP/1.1", "http/1.1", False),
                                     ("HTTPS/1.1", "http/1.1", True),
                                     ("HTTPS/2", "h2", True)):
            with peer(protocol, cert if tls else None, key if tls else None) as (server, origin):
                run(token_file, origin, cert if tls else "-")
                assert not server.errors, server.errors
                assert collections.Counter(x[0] for x in server.calls) == collections.Counter(range(1, 7)), server.calls
                ids = {item: connection for item, connection, _, _ in server.calls}
                assert len({ids[1], ids[2], ids[3], ids[4], ids[6]}) == 5, ids
                assert ids[4] == ids[5], "trusted poll did not reuse its connection"
                if tls:
                    assert server.negotiated and set(server.negotiated) == {protocol}
                print(f"{label}: complete requests 1..6 once each; dropped reply stays ambiguous; distinct effect connections, poll reuse, no redirect")
        with peer("h2", cert, key) as (server, origin):
            run(token_file, origin, "-", "tls-failure")
            assert not server.calls and not server.errors
        with peer("h2", wrong, wrong_key) as (server, origin):
            run(token_file, origin, wrong, "tls-failure")
            assert not server.calls and not server.errors
        print("Untrusted CA and wrong hostname: no HTTP request reached the peer")


if __name__ == "__main__":
    main()
