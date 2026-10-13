#!/usr/bin/env python3
"""Credential aliases never become guest atoms; scope failures do no I/O."""
import http.server
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading


class Peer(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        self.server.seen.append(self.headers.get("Authorization"))
        self.rfile.read(int(self.headers.get("Content-Length", 0)))
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b"fixture-token" if self.path.endswith("echo") else b"ok")


def main():
    binary = str(Path(sys.argv[1]).resolve())
    peer = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Peer)
    peer.seen = []
    threading.Thread(target=peer.serve_forever, daemon=True).start()
    base = f"http://127.0.0.1:{peer.server_port}/v1/"
    environment = {**os.environ, "CETTA_IO_AUTH_FIXTURE_URL_PREFIX": base,
        "CETTA_IO_AUTH_FIXTURE_HEADER": "Authorization", "CETTA_IO_AUTH_FIXTURE_ENV": "IO_FIXTURE_TOKEN",
        "CETTA_IO_AUTH_FIXTURE_PREFIX": "Bearer ", "IO_FIXTURE_TOKEN": "fixture-token"}
    try:
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "credential.metta"
            for language in ("he", "petta"):
                for tail, headers, follow, sent, successful in [
                    ("complete", '((http:credential "FIXTURE"))', "", True, True),
                    ("echo", '((http:credential "FIXTURE"))', "", True, False),
                    ("../outside", '((http:credential "FIXTURE"))', "", False, False),
                    ("%2e%2e/outside", '((http:credential "FIXTURE"))', "", False, False),
                    ("complete", '((http:credential "FIXTURE"))', " True", False, False),
                    ("complete", '((http:credential "UNKNOWN"))', "", False, False),
                    ("complete", '((http:credential "FIXTURE") (http:header "authorization" "override"))', "", False, False),
                    ("complete", '((http:credential "FIXTURE") (http:credential "FIXTURE"))', "", False, False)]:
                    before = len(peer.seen)
                    opening = "(catch " if language == "petta" else ""
                    closing = ")" if language == "petta" else ""
                    source.write_text('!(import! &self io)\n' +
                        '(= (fixture-wait) (let $event (io:wait 100)\n'
                        ' (case $event (((io:idle) (fixture-wait)) ($done $done)))))\n' +
                        f'!(let $submitted {opening}(io:submit (http:request "POST" {json.dumps(base+tail)}\n'
                        f' {headers} "{{}}" 2000 1024{follow})){closing}\n'
                        ' (case $submitted (((io:pending $id) (fixture-wait)) ($failure $failure))))\n')
                    result = subprocess.run([binary, "--lang", language, "--quiet", str(source)],
                        env=environment, capture_output=True, text=True, timeout=10)
                    output = result.stdout + result.stderr
                    assert result.returncode == 0, output
                    assert "fixture-token" not in output, "credential reached guest output"
                    assert len(peer.seen) == before + int(sent), (language, tail, output)
                    assert ("http:response 200" in output) == successful, (language, tail, output)
                    if sent:
                        assert peer.seen[-1] == "Bearer fixture-token"
        print("Credential aliases: host-only keys, path scope, redirects, echo and overrides passed on HE/PeTTa")
    finally:
        peer.shutdown()
        peer.server_close()


if __name__ == "__main__":
    main()
