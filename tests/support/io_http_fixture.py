#!/usr/bin/env python3

import argparse
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import sys
import time


class Handler(SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/slow":
            time.sleep(0.2)
            body = b"slow"
        elif self.path == "/large":
            body = b"response-larger-than-four-bytes"
        elif self.path == "/one":
            body = b"one"
        elif self.path == "/two":
            body = b"two"
        else:
            return super().do_GET()
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
        except BrokenPipeError:
            pass

    def log_message(self, _format, *_args):
        pass


parser = argparse.ArgumentParser()
parser.add_argument("--root", default=None)
args = parser.parse_args()


def handler(*handler_args, **handler_kwargs):
    return Handler(*handler_args, directory=args.root, **handler_kwargs)


server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
print(server.server_port, flush=True)
server.serve_forever()
