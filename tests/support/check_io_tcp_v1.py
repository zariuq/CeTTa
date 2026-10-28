#!/usr/bin/env python3
"""TCP requests of the io runtime against a local server.

The server's behavior is chosen by the first line each connection sends.
Each case is one MeTTa program run in PeTTa; its printed result and what the
server observed are both checked:

- lines arrive without their CRLF or LF, multilingual text whole, bytes that
  are not UTF-8 as U+FFFD, and an unterminated last line when the peer ends
  the stream, after which a receive completes tcp:closed;
- text sent is written as given, in order, and tcp:close writes what is
  queued before it closes;
- with pieces, a character split across TCP segments arrives whole;
- a line longer than max-bytes closes the connection with a reason;
- no listener answers tcp:failed with curl's code, and a certificate nobody
  vouches for fails TLS, unless the connection's (ca-file PATH) names it,
  when the text goes through TLS both ways; a malformed option is refused;
- a released connection, and a WebSocket operation on a TCP connection, are
  refused;
- 400 lines sent while the program sleeps all arrive, in order, through a
  queue limited to 16 KiB, so reading paused and resumed without loss.
"""
import argparse
import hashlib
import re
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

FLOOD_COUNT = 400
REPLACEMENT = "\ufffd"


def flood_text(i):
    return "%04d " % i + "f" * 200


class Server:
    """One listening socket; each connection names its behavior first."""

    def __init__(self, context=None):
        self.observed = {}
        self.context = context
        self.socket = socket.socket()
        self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.socket.bind(("127.0.0.1", 0))
        self.socket.listen(16)
        self.port = self.socket.getsockname()[1]
        threading.Thread(target=self._accept, daemon=True).start()

    def _accept(self):
        while True:
            try:
                connection, _ = self.socket.accept()
            except OSError:
                return
            threading.Thread(target=self._serve, args=(connection,), daemon=True).start()

    def _serve(self, connection):
        try:
            if self.context:
                connection = self.context.wrap_socket(connection, server_side=True)
            stream = connection.makefile("rb")
            behavior = stream.readline().decode().strip()
            if behavior == "echo":
                received = []
                for line in stream:
                    received.append(line.decode())
                    connection.sendall(line)
                self.observed[behavior] = received
            elif behavior == "push":
                connection.sendall("one\r\ntwo\nπλάτων ✓ 🦄\n".encode() + b"\xff ok\n" + b"tail")
            elif behavior == "split":
                connection.sendall(b"\xc3")
                time.sleep(0.3)
                connection.sendall(b"\xa9!")
            elif behavior == "long":
                connection.sendall(b"x" * 100 + b"\n")
                stream.read()
            elif behavior == "flood":
                connection.sendall("".join(flood_text(i) + "\n" for i in range(FLOOD_COUNT)).encode())
            elif behavior == "quiet":
                self.observed[behavior] = stream.read().decode()
        except (OSError, ssl.SSLError):
            pass
        finally:
            connection.close()

    def stop(self):
        self.socket.close()


PROGRAM_HEAD = "!(import! &self (library io))\n!(import! &self (library str))\n"
WAIT = "(io:wait %s 5000)"
DRAIN = """
(= (drain $c 0 $digest) $digest)
(= (drain $c $n $digest)
   (let* (((io:pending $r) (io:submit (tcp:receive $c))) ((io:event $r (tcp:received $c $line)) (io:wait $r 5000)))
     (drain $c (- $n 1) (str:sha256 (str:concat $digest $line)))))
"""


def connect(url, framing="lines", limit=4096, options=""):
    return '((io:pending $c) (io:submit (tcp:connect "%s" 5000 %d %s%s)))' % (url, limit, framing, options)


def send(name, text):
    return "((io:pending $%s) (io:submit (tcp:send $c %s))) ($%s %s)" % (
        name, '"' + text.replace("\n", "\\n").replace("\r", "\\r") + '"', name.upper(), WAIT % ("$" + name))


def receive(name):
    return "((io:pending $%s) (io:submit (tcp:receive $c))) ($%s %s)" % (name, name.upper(), WAIT % ("$" + name))


def cases(url):
    return [
        ("lines",
         "!(let* (%s ($o %s) %s %s %s %s %s %s %s) (println! (RESULT $o $S $R1 $R2 $R3 $R4 $R5 $R6)))"
         % (connect(url), WAIT % "$c", send("s", "push\n"), receive("r1"), receive("r2"), receive("r3"),
            receive("r4"), receive("r5"), receive("r6")),
         ['(RESULT (io:event C (tcp:connected C)) (io:event S (tcp:sent C)) (io:event R (tcp:received C "one"))'
          ' (io:event R (tcp:received C "two")) (io:event R (tcp:received C "πλάτων ✓ 🦄"))'
          ' (io:event R (tcp:received C "' + REPLACEMENT + ' ok")) (io:event R (tcp:received C "tail"))'
          ' (io:event R (tcp:closed C "")))'],
         {}),
        ("echo",
         "!(let* (%s ($o %s) %s %s %s %s ((io:pending $x) (io:submit (tcp:close $c))) ($cx %s))"
         " (println! (RESULT $S $T $R1 $R2 $cx)))"
         % (connect(url), WAIT % "$c", send("s", "echo\nhello\r\n"), send("t", "ελληνικά\n"),
            receive("r1"), receive("r2"), WAIT % "$x"),
         ['(RESULT (io:event S (tcp:sent C)) (io:event S (tcp:sent C)) (io:event R (tcp:received C "hello"))'
          ' (io:event R (tcp:received C "ελληνικά")) (io:event X (tcp:closed C "")))'],
         {"echo": ["hello\r\n", "ελληνικά\n"]}),
        # The server's record shows that what was queued went out first.
        ("close writes what is queued",
         "!(let* (%s ($o %s) ((io:pending $s) (io:submit (tcp:send $c \"quiet\\nlast words\")))"
         " ((io:pending $x) (io:submit (tcp:close $c))) ($cx %s)) (println! (RESULT $cx)))"
         % (connect(url), WAIT % "$c", WAIT % "$x"),
         ['(RESULT (io:event X (tcp:closed C "")))'],
         {"quiet": "last words"}),
        ("pieces keep characters whole",
         "!(let* (%s ($o %s) %s %s %s) (println! (RESULT $R1 $R2)))"
         % (connect(url, "pieces"), WAIT % "$c", send("s", "split\n"), receive("r1"), receive("r2")),
         ['(RESULT (io:event R (tcp:received C "é!")) (io:event R (tcp:closed C "")))'],
         {}),
        ("a line too long",
         "!(let* (%s ($o %s) %s %s) (println! (RESULT $R1)))"
         % (connect(url, "lines", 50), WAIT % "$c", send("s", "long\n"), receive("r1")),
         ['(RESULT (io:event R (tcp:closed C "line longer than max-bytes")))'],
         {}),
        # curl's wording varies by version; its code does not.
        ("no listener",
         "!(let* (%s ((io:event $c (tcp:failed $c $code $text)) %s)) (println! (RESULT $code)))"
         % (connect("tcp://127.0.0.1:1"), WAIT % "$c"),
         ["(RESULT 7)"],
         {}),
        ("released and mismatched connections are refused",
         "!(let* (%s ($o %s) ((io:pending $x) (io:submit (tcp:close $c))) ($cx %s) ($late (io:submit (tcp:receive $c))))"
         " (println! (RESULT $late)))\n"
         "!(let* (%s ($o %s) ($wrong (io:submit (ws:receive $c)))) (println! (RESULT $wrong)))"
         % (connect(url), WAIT % "$c", WAIT % "$x", connect(url), WAIT % "$c"),
         ['(RESULT (Error (io:submit) "expected a TCP connection this runtime opened and has not released"))',
          '(RESULT (Error (io:submit) "expected a WebSocket connection this runtime opened and has not released"))'],
         {}),
        ("flood through a small queue",
         DRAIN + "!(let* (%s ($o %s) %s ((io:pending $t) (io:submit (timer:after 1000))) ($slept %s)"
         " ($digest (drain $c %d \"\"))) (println! (RESULT $digest)))"
         % (connect(url, "lines", 16384), WAIT % "$c", send("s", "flood\n"), WAIT % "$t", FLOOD_COUNT),
         ['(RESULT "%s")' % flood_digest()],
         {}),
    ]


def flood_digest():
    digest = ""
    for i in range(FLOOD_COUNT):
        digest = hashlib.sha256((digest + flood_text(i)).encode()).hexdigest()
    return digest


def normalize(line):
    """Request and connection IDs differ by run; both sides name them '?'."""
    return re.sub(r"\(io:event \d+ \((tcp:[a-z]+) \d+", lambda m: "(io:event ? (%s ?" % m.group(1), line)


def shape(text):
    """The wanted lines name a request and a connection by letters."""
    return re.sub(r"\(io:event [A-Z] \((tcp:[a-z]+) [A-Z]", lambda m: "(io:event ? (%s ?" % m.group(1), text)


def run(binary, directory, body):
    program = Path(directory) / "case.metta"
    program.write_text(PROGRAM_HEAD + body + "\n", encoding="utf-8")
    result = subprocess.run([str(binary), "--lang", "petta", str(program)],
                            capture_output=True, encoding="utf-8", errors="replace", timeout=120)
    return result, [normalize(line) for line in result.stdout.split("\n") if line.startswith("(RESULT")]


def tls_cases(binary, directory):
    """A TLS server whose self-signed certificate no authority vouches for:
    the connection fails with curl's verification code, 60, and succeeds
    when its (ca-file PATH) names that certificate."""
    if not shutil.which("openssl"):
        return None
    key, cert = Path(directory) / "key.pem", Path(directory) / "cert.pem"
    made = subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", str(key),
                           "-out", str(cert), "-days", "1", "-subj", "/CN=127.0.0.1",
                           "-addext", "subjectAltName=IP:127.0.0.1"], capture_output=True)
    if made.returncode:
        return None
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(cert, key)
    server = Server(context)
    url = "tls://127.0.0.1:%d" % server.port
    trusted = ' ((ca-file "%s"))' % cert
    untrusted = run(binary, directory, "!(let* (%s ((io:event $c (tcp:failed $c $code $text)) %s))"
                    " (println! (RESULT $code)))" % (connect(url), WAIT % "$c"))
    echoed = run(binary, directory, "!(let* (%s ($o %s) %s %s) (println! (RESULT $o $R1)))"
                 % (connect(url, options=trusted), WAIT % "$c", send("s", "echo\nsecret\n"), receive("r1")))
    malformed = run(binary, directory, '!(println! (RESULT (io:submit (tcp:connect "%s" 5000 4096 lines ((ca-file 7))))))' % url)
    server.stop()
    return [("tls without a trusted certificate", untrusted, ["(RESULT 60)"]),
            ("tls with the certificate as its ca-file", echoed,
             [shape('(RESULT (io:event C (tcp:connected C)) (io:event R (tcp:received C "secret")))')]),
            ("a malformed connection option", malformed,
             ['(RESULT (Error (io:submit) "expected (tcp:connect url connect-timeout-ms max-bytes lines|pieces), '
              'optionally followed by ((ca-file path))"))'])]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("binary", type=Path)
    arguments = parser.parse_args()
    binary = arguments.binary.resolve()
    with tempfile.TemporaryDirectory() as directory:
        result, _ = run(binary, directory, "!(println! (io:capabilities))")
    if "tcp" not in result.stdout.replace("(", " ").replace(")", " ").split():
        print("io tcp: skipped, this build has no TCP provider")
        return 0
    server = Server()
    url = "tcp://127.0.0.1:%d" % server.port
    failures, total = [], 0
    with tempfile.TemporaryDirectory() as directory:
        for name, body, want, observed in cases(url):
            result, got = run(binary, directory, body)
            total += 1
            if result.returncode or got != [shape(w) for w in want]:
                failures.append((name, result.returncode, got, [shape(w) for w in want], result.stderr[-600:]))
            for behavior, facts in observed.items():
                total += 1
                for _ in range(40):
                    if behavior in server.observed:
                        break
                    time.sleep(0.05)
                if server.observed.get(behavior) != facts:
                    failures.append((name + " (server)", 0, server.observed.get(behavior), facts, ""))
        for name, (result, got), want in tls_cases(binary, directory) or []:
            total += 1
            if got != want:
                failures.append((name, result.returncode, got, want, result.stderr[-600:]))
    server.stop()
    for name, code, got, want, err in failures:
        print("FAIL %s (exit %s):\n  got  %s\n  want %s\n  %s" % (name, code, str(got)[:600], want, err.strip()[:300]))
    print("io tcp: %d checks, %d failures" % (total, len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
