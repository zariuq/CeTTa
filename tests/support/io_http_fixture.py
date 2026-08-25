#!/usr/bin/env python3

import argparse
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import sys
import time
from urllib.parse import urlsplit


class Handler(SimpleHTTPRequestHandler):
    def _send(self, status, body, extra_headers=()):
        self.send_response(status)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        for key, value in extra_headers:
            self.send_header(key, value)
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_GET(self):
        path = urlsplit(self.path).path
        if path == "/slow":
            time.sleep(0.2)
            body = b"slow"
        elif path == "/large":
            body = b"response-larger-than-four-bytes"
        elif path == "/one":
            body = b"one"
        elif path == "/two":
            body = b"two"
        elif path.startswith("/bytes/"):
            try:
                size = int(path.rsplit("/", 1)[1])
            except ValueError:
                return self._send(400, b"invalid byte count")
            if size < 0 or size > 1048576:
                return self._send(400, b"invalid byte count")
            body = bytes((97 + index % 26 for index in range(size)))
        elif path.startswith("/status/"):
            try:
                status = int(path.rsplit("/", 1)[1])
            except ValueError:
                return self._send(400, b"invalid status")
            return self._send(status, ("status-%d" % status).encode())
        elif path.startswith("/redirect/"):
            try:
                remaining = int(path.rsplit("/", 1)[1])
            except ValueError:
                return self._send(400, b"invalid redirect count")
            destination = ("/redirect/%d" % (remaining - 1)
                           if remaining > 1 else "/one")
            return self._send(302, b"", (("Location", destination),))
        elif path == "/drip":
            chunks = 64
            chunk = b"0123456789abcdef" * 64
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(chunks * len(chunk)))
            self.end_headers()
            for _ in range(chunks):
                try:
                    self.wfile.write(chunk)
                    self.wfile.flush()
                except (BrokenPipeError, ConnectionResetError):
                    break
                time.sleep(0.005)
            return
        else:
            return super().do_GET()
        self._send(200, body)

    def do_POST(self):
        path = urlsplit(self.path).path
        if path != "/echo":
            return self._send(404, b"not found")
        try:
            size = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            return self._send(400, b"invalid content length")
        self._send(200, self.rfile.read(size))

    def log_message(self, _format, *_args):
        pass


class Server(ThreadingHTTPServer):
    daemon_threads = True
    request_queue_size = 256


parser = argparse.ArgumentParser()
parser.add_argument("--root", default=None)
args = parser.parse_args()


def handler(*handler_args, **handler_kwargs):
    return Handler(*handler_args, directory=args.root, **handler_kwargs)


server = Server(("127.0.0.1", 0), handler)
print(server.server_port, flush=True)
server.serve_forever()
