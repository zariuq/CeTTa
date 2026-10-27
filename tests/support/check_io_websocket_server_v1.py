#!/usr/bin/env python3
"""WebSocket listeners of the io runtime against clients.

Each case runs a PeTTa program that listens on a free port and prints it,
while this script plays the clients: Python's websockets package for ordinary
ones, raw sockets for handshakes and frames no library sends.  The program's
printed results and what the clients observed are both checked:

- the opening handshake: the Sec-WebSocket-Accept of RFC 6455's example, the
  request target, peer and header fields handed to the program, frames sent
  right behind the request, and refusals: 400 (missing or repeated fields,
  a malformed key, HTTP/1.0, a line without a colon, a folded line, a bare
  LF), 405 for other methods, 426 without the upgrade or with another
  version, 431 for a request over 16 KiB, a silent client dropped after
  idle-ms;
- echo of ASCII, multilingual, empty and limit-size text, in order, with the
  payload lengths around each frame-length encoding sent back unmangled;
  fragmented messages, also with a ping between fragments;
- refusals by close code: 1009 one byte past the limit, 1003 binary, 1007
  text or a close reason that is not UTF-8, 1002 for an unmasked frame,
  reserved bits, an unknown opcode, a fragmented or long control frame, a
  stray continuation, a new message inside a fragmented one, a one-byte
  close payload, an invalid close code and an out-of-range length;
- pings answered with their payload; with idle-ms set, a ping after silence,
  a client that answers kept, one that does not dropped (1006);
- closes from either side with code and reason, a close the client never
  answers completing after five seconds;
- max-connections: the next client waits until a connection ends;
- an accept cancelled after a connection was assigned hands it to the next
  accept; ws:stop completes pending accepts, refuses new clients and leaves
  accepted connections working; listen failures are completions;
- 400 messages from a client while the program sleeps all arrive, in order,
  through a 16 KiB queue, the sleep outlasting twice idle-ms;
- the io runtime's own client (ws:connect) against its listener, both ways.
"""
import argparse
import base64
import hashlib
import os
import queue
import re
import socket
import subprocess
import sys
import tempfile
import threading
import time
import traceback
from pathlib import Path

try:
    from websockets.sync.client import connect
except ImportError:  # pragma: no cover - reported as a skip below
    connect = None

HEAD = "!(import! &self (library io))\n!(import! &self (library str))\n"

# Accepts COUNT connections one after another; prints each accept, each
# message's length and digest, and how each connection ended.
SINK = HEAD + r'''
(= (drain $c)
   (let* (((io:pending $r) (io:submit (ws:receive $c))) ((io:event $r $e) (io:wait $r 30000)))
     (case $e
       (((ws:message $c $text)
         (let $p (println! (RESULT message (str:byte-length $text) (str:sha256 $text))) (drain $c)))
        ($other (let* (($p (println! (RESULT end $other)))
                       ((io:pending $x) (io:submit (ws:close $c 1000 ""))) ($y (io:wait $x 10000)))
                  closed))))))
(= (serve $l $n)
   (if (== $n 0) done
     (let* (((io:pending $a) (io:submit (ws:accept $l))) ((io:event $a $accepted) (io:wait $a 60000)))
       (case $accepted
         (((ws:accepted $c $target $peer $fields)
           (let* (($p (println! (RESULT accepted $c $target $peer $fields))) ($d (drain $c))) (serve $l (- $n 1))))
          ($other (println! (RESULT accept $other))))))))
!(let* (((io:pending $l) (io:submit (ws:listen "127.0.0.1" 0 LIMIT CONNECTIONS IDLE)))
        ((io:event $l $listening) (io:wait $l 5000)))
   (case $listening
     (((ws:listening $l $port) (let $p (println! (LISTENING $port)) (println! (RESULT (serve $l COUNT)))))
      ($other (println! (RESULT listen $other))))))
'''

# Echoes every message of one connection until it ends.
ECHO = HEAD + r'''
(= (echo $c)
   (let* (((io:pending $r) (io:submit (ws:receive $c))) ((io:event $r $e) (io:wait $r 30000)))
     (case $e
       (((ws:message $c $text)
         (let* (((io:pending $s) (io:submit (ws:send $c $text))) ($sent (io:wait $s 10000))) (echo $c)))
        ($other (println! (RESULT end $other)))))))
!(let* (((io:pending $l) (io:submit (ws:listen "127.0.0.1" 0 LIMIT 4 IDLE)))
        ((io:event $l (ws:listening $l $port)) (io:wait $l 5000))
        ($p (println! (LISTENING $port)))
        ((io:pending $a) (io:submit (ws:accept $l)))
        ((io:event $a (ws:accepted $c $target $peer $fields)) (io:wait $a 30000)))
   (let $q (println! (RESULT accepted $c $target $peer $fields)) (echo $c)))
'''

LENGTHS = [0, 1, 125, 126, 127, 65535, 65536, 70000]

# Sends messages of LENGTHS, then closes with 4000 "bye".
PUSH = HEAD + r'''
(= (sized $n)
   (if (== $n 0) ""
     (if (== (% $n 2) 0)
       (let $half (sized (/ $n 2)) (str:concat $half $half))
       (str:concat "p" (sized (- $n 1))))))
!(let* (((io:pending $l) (io:submit (ws:listen "127.0.0.1" 0 1048576 4 0)))
        ((io:event $l (ws:listening $l $port)) (io:wait $l 5000))
        ($p (println! (LISTENING $port)))
        ((io:pending $a) (io:submit (ws:accept $l)))
        ((io:event $a (ws:accepted $c $target $peer $fields)) (io:wait $a 30000))
        SENDS
        ((io:pending $x) (io:submit (ws:close $c 4000 "bye")))
        ($closed (io:wait $x 10000)))
   (println! (RESULT done $closed)))
'''

# A close the client never answers.
CLOSE_UNANSWERED = HEAD + r'''
!(let* (((io:pending $l) (io:submit (ws:listen "127.0.0.1" 0 1024 4 0)))
        ((io:event $l (ws:listening $l $port)) (io:wait $l 5000))
        ($p (println! (LISTENING $port)))
        ((io:pending $a) (io:submit (ws:accept $l)))
        ((io:event $a (ws:accepted $c $target $peer $fields)) (io:wait $a 30000))
        ($q (println! (RESULT accepted)))
        ((io:pending $x) (io:submit (ws:close $c 1000 "now")))
        ($closed (io:wait $x 20000)))
   (println! (RESULT $closed)))
'''

# The first accept is cancelled after a connection was assigned to it.
CANCEL = HEAD + r'''
!(let* (((io:pending $l) (io:submit (ws:listen "127.0.0.1" 0 1024 4 0)))
        ((io:event $l (ws:listening $l $port)) (io:wait $l 5000))
        ($p (println! (LISTENING $port)))
        ((io:pending $a) (io:submit (ws:accept $l)))
        ($ready (io:wait-ready (io:ids $a) 30000))
        ($cancelled (io:cancel $a))
        ((io:pending $b) (io:submit (ws:accept $l)))
        ((io:event $b (ws:accepted $c $target $peer $fields)) (io:wait $b 5000))
        ((io:pending $r) (io:submit (ws:receive $c)))
        ((io:event $r $m) (io:wait $r 10000)))
   (println! (RESULT (== $ready (io:ready ($a))) $target $m)))
'''

# ws:stop: a pending accept completes, the accepted connection keeps working.
STOP = HEAD + r'''
!(let* (((io:pending $l) (io:submit (ws:listen "127.0.0.1" 0 1024 4 0)))
        ((io:event $l (ws:listening $l $port)) (io:wait $l 5000))
        ($p (println! (LISTENING $port)))
        ((io:pending $a) (io:submit (ws:accept $l)))
        ((io:event $a (ws:accepted $c $target $peer $fields)) (io:wait $a 30000))
        ((io:pending $b) (io:submit (ws:accept $l)))
        ((io:pending $s) (io:submit (ws:stop $l)))
        ((io:event $s $stopped) (io:wait $s 5000))
        ((io:event $b $pending) (io:wait $b 5000))
        ($again (io:submit (ws:accept $l)))
        ($q (println! (RESULT stopped)))
        ((io:pending $r) (io:submit (ws:receive $c)))
        ((io:event $r $m) (io:wait $r 10000))
        ((io:pending $x) (io:submit (ws:close $c 1000 "")))
        ($closed (io:wait $x 10000)))
   (println! (RESULT $stopped $pending (case $again (((Error $call $why) refused) ($other $other))) $m)))
'''

# Listen failures are completions; malformed requests are refused at submit.
FAILURES = HEAD + r'''
!(let* (((io:pending $l) (io:submit (ws:listen "127.0.0.1" BUSY 1024 4 0))) ((io:event $l $e) (io:wait $l 5000)))
   (println! (RESULT $e)))
!(let* (((io:pending $l) (io:submit (ws:listen "host.invalid" 0 1024 4 0))) ((io:event $l $e) (io:wait $l 5000)))
   (println! (RESULT (case $e (((ws:listen-failed $id $why) listen-failed) ($other $other))))))
!(println! (RESULT (case (io:submit (ws:listen "127.0.0.1" 70000 1024 4 0)) (((Error $call $why) refused) ($other $other)))))
!(println! (RESULT (case (io:submit (ws:listen "127.0.0.1" 0 0 4 0)) (((Error $call $why) refused) ($other $other)))))
!(println! (RESULT (case (io:submit (ws:listen "127.0.0.1" 0 1024 0 0)) (((Error $call $why) refused) ($other $other)))))
!(println! (RESULT (case (io:submit (ws:accept 999)) (((Error $call $why) refused) ($other $other)))))
!(println! (RESULT (case (io:submit (ws:stop 999)) (((Error $call $why) refused) ($other $other)))))
!(println! (RESULT (io:capabilities)))
'''

# The runtime's client against its own listener.
LOOPBACK = HEAD + r'''
!(let* (((io:pending $l) (io:submit (ws:listen "127.0.0.1" 0 65536 4 0)))
        ((io:event $l (ws:listening $l $port)) (io:wait $l 5000))
        ((io:pending $k) (io:submit (ws:connect (str:concat "ws://127.0.0.1:" (str:concat (repr $port) "/loop")) 5000 65536)))
        ((io:pending $a) (io:submit (ws:accept $l)))
        ((io:event $a (ws:accepted $c $target $peer $fields)) (io:wait $a 10000))
        ((io:event $k $connected) (io:wait $k 10000))
        ((io:pending $s1) (io:submit (ws:send $k "from the client ✓"))) ($w1 (io:wait $s1 5000))
        ((io:pending $r1) (io:submit (ws:receive $c))) ((io:event $r1 $m1) (io:wait $r1 5000))
        ((io:pending $s2) (io:submit (ws:send $c "from the server ✓"))) ($w2 (io:wait $s2 5000))
        ((io:pending $r2) (io:submit (ws:receive $k))) ((io:event $r2 $m2) (io:wait $r2 5000))
        ((io:pending $x) (io:submit (ws:close $k 4002 "loop done"))) ((io:event $x $kc) (io:wait $x 10000))
        ((io:pending $r3) (io:submit (ws:receive $c))) ((io:event $r3 $m3) (io:wait $r3 10000)))
   (println! (RESULT $target $m1 $m2 $kc $m3)))
'''

FLOOD_COUNT = 400

# Sleeps while a client floods a 16 KiB queue, then reads everything.  The
# sleep is longer than twice idle-ms: a full queue is not a silent client.
FLOOD = HEAD + r'''
(= (drain $c $n $digest)
   (if (== $n 0) $digest
     (let* (((io:pending $r) (io:submit (ws:receive $c))) ((io:event $r (ws:message $c $text)) (io:wait $r 10000)))
       (drain $c (- $n 1) (str:sha256 (str:concat $digest $text))))))
!(let* (((io:pending $l) (io:submit (ws:listen "127.0.0.1" 0 16384 4 200)))
        ((io:event $l (ws:listening $l $port)) (io:wait $l 5000))
        ($p (println! (LISTENING $port)))
        ((io:pending $a) (io:submit (ws:accept $l)))
        ((io:event $a (ws:accepted $c $target $peer $fields)) (io:wait $a 30000))
        ((io:pending $t) (io:submit (timer:after 900))) ($slept (io:wait $t 5000))
        ($digest (drain $c COUNT "")))
   (println! (RESULT $digest)))
'''


class Program:
    """A running case program whose output lines arrive on a queue."""

    def __init__(self, binary, directory, source, name):
        path = Path(directory) / (name + ".metta")
        path.write_text(source, encoding="utf-8")
        self.process = subprocess.Popen([str(binary), "--lang", "petta", str(path)], stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, encoding="utf-8", errors="replace")
        self.lines = queue.Queue()
        threading.Thread(target=self._read, daemon=True).start()
        self.port = None
        if "LISTENING" in source:
            line = self.next(20)
            match = re.match(r"\(LISTENING (\d+)\)", line or "")
            if not match:
                raise RuntimeError("no listener: %r %s" % (line, self.stderr()))
            self.port = int(match.group(1))

    def _read(self):
        for line in self.process.stdout:
            self.lines.put(line.rstrip("\n"))
        self.lines.put(None)

    def next(self, timeout=30):
        try:
            return self.lines.get(timeout=timeout)
        except queue.Empty:
            return None

    def result(self, timeout=30):
        """The next RESULT line, without the wrapper, or None."""
        while True:
            line = self.next(timeout)
            if line is None:
                return None
            if line.startswith("(RESULT "):
                return line[len("(RESULT "):-1]

    def stderr(self):
        try:
            self.process.wait(5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(5)
        return self.process.stderr.read()[-600:]

    def finish(self):
        try:
            return self.process.wait(30)
        except subprocess.TimeoutExpired:
            self.process.kill()
            return self.process.wait(5)


def frame(opcode, payload=b"", fin=True, mask=True, rsv=0, length=None):
    first = (0x80 if fin else 0) | rsv | opcode
    n = len(payload) if length is None else length
    bit = 0x80 if mask else 0
    if n < 126:
        header = bytes([first, bit | n])
    elif n < 65536:
        header = bytes([first, bit | 126]) + n.to_bytes(2, "big")
    else:
        header = bytes([first, bit | 127]) + n.to_bytes(8, "big")
    if not mask:
        return header + payload
    key = os.urandom(4)
    return header + key + bytes(b ^ key[i % 4] for i, b in enumerate(payload))


class Raw:
    """A client on a plain socket."""

    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=10)
        self.buffer = b""

    def request(self, text):
        self.sock.sendall(text if isinstance(text, bytes) else text.encode("latin-1"))

    def response(self):
        """The status line and fields of the HTTP response, lowercased names."""
        while b"\r\n\r\n" not in self.buffer:
            chunk = self.sock.recv(65536)
            if not chunk:
                return None, {}
            self.buffer += chunk
        head, self.buffer = self.buffer.split(b"\r\n\r\n", 1)
        lines = head.decode("latin-1").split("\r\n")
        fields = {}
        for line in lines[1:]:
            name, _, value = line.partition(":")
            fields[name.strip().lower()] = value.strip()
        return lines[0], fields

    def upgrade(self, path="/", key="dGhlIHNhbXBsZSBub25jZQ==", after=b""):
        self.request(("GET %s HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                      "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n" % (path, key)).encode() + after)
        return self.response()

    def _need(self, n):
        while len(self.buffer) < n:
            chunk = self.sock.recv(65536)
            if not chunk:
                return False
            self.buffer += chunk
        return True

    def read_frame(self):
        """(fin, opcode, payload) of the next server frame, or None at the end."""
        try:
            if not self._need(2):
                return None
            first, second = self.buffer[0], self.buffer[1]
            n, at = second & 0x7f, 2
            if n == 126:
                if not self._need(4):
                    return None
                n, at = int.from_bytes(self.buffer[2:4], "big"), 4
            elif n == 127:
                if not self._need(10):
                    return None
                n, at = int.from_bytes(self.buffer[2:10], "big"), 10
            if second & 0x80 or not self._need(at + n):
                return ("masked",) if second & 0x80 else None
            self.header_length = at
            payload, self.buffer = self.buffer[at:at + n], self.buffer[at + n:]
            return bool(first & 0x80), first & 0x0f, payload
        except (ConnectionResetError, socket.timeout):
            return None

    def close_code(self):
        """The code of the next close frame, skipping pings; None at the end."""
        while True:
            got = self.read_frame()
            if got is None or got[0] == "masked":
                return got
            if got[1] == 8:
                return int.from_bytes(got[2][:2], "big") if len(got[2]) >= 2 else 1005
            if got[1] != 9:
                return ("unexpected", got)

    def ended(self):
        """Whether the server closed the socket (reads until the end)."""
        try:
            while True:
                chunk = self.sock.recv(65536)
                if not chunk:
                    return True
        except (ConnectionResetError, BrokenPipeError):
            return True
        except socket.timeout:
            return False

    def close(self):
        self.sock.close()


def digest(text):
    data = text.encode("utf-8", "surrogatepass")
    return '%d "%s"' % (len(data), hashlib.sha256(data).hexdigest())


class Checks:
    def __init__(self):
        self.failures, self.total = [], 0

    def expect(self, name, got, want):
        self.total += 1
        ok = want(got) if callable(want) else got == want
        if not ok:
            self.failures.append((name, got, "a match" if callable(want) else want))


def handshake_cases(binary, directory, checks):
    accept = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="
    base = ("GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n")
    refusals = [
        ("no key", base.replace("Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n", "") + "\r\n", "400"),
        ("two keys", base + "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n", "400"),
        ("a key of 15 bytes", base.replace("dGhlIHNhbXBsZSBub25jZQ==", "dGhlIHNhbXBsZSBub25jZQ") + "\r\n", "400"),
        ("a key not base64", base.replace("dGhlIHNhbXBsZSBub25jZQ==", "dGhlIHNhbXBsZSBub25jZ!==") + "\r\n", "400"),
        ("no host", base.replace("Host: 127.0.0.1\r\n", "") + "\r\n", "400"),
        ("two hosts", base + "Host: other\r\n\r\n", "400"),
        ("no version", base.replace("Sec-WebSocket-Version: 13\r\n", "") + "\r\n", "400"),
        ("HTTP/1.0", base.replace("HTTP/1.1", "HTTP/1.0", 1) + "\r\n", "400"),
        ("a line without a colon", base + "Broken\r\n\r\n", "400"),
        ("a folded line", base + "X-A: one\r\n two\r\n\r\n", "400"),
        ("a folded line with a colon", base + "X-A: one\r\n\tx: y\r\n\r\n", "400"),
        ("a name with a space", base + "Bad Name: x\r\n\r\n", "400"),
        ("a bare LF", base.replace("Connection: Upgrade\r\n", "Connection: Upgrade\nX: y\r\n") + "\r\n", "400"),
        ("POST", base.replace("GET", "POST", 1) + "\r\n", "405"),
        ("no upgrade", base.replace("Upgrade: websocket\r\n", "") + "\r\n", "426"),
        ("connection keep-alive only", base.replace("Connection: Upgrade", "Connection: keep-alive") + "\r\n", "426"),
        ("version 8", base.replace("Version: 13", "Version: 8") + "\r\n", "426"),
        ("a request over 16 KiB", base + "X-Long: " + "a" * 17000 + "\r\n\r\n", "431"),
    ]
    program = Program(binary, directory, SINK.replace("LIMIT", "1024").replace("CONNECTIONS", "64")
                      .replace("IDLE", "400").replace("COUNT", "3"), "handshake")
    for name, text, status in refusals:
        raw = Raw(program.port)
        raw.request(text)
        line, fields = raw.response()
        checks.expect("handshake: " + name, (line or "").split(" ")[1:2], [status])
        if status == "426" and name == "version 8":
            checks.expect("handshake: version 8 names 13", fields.get("sec-websocket-version"), "13")
        if status == "426" and name == "no upgrade":
            checks.expect("handshake: 426 names the upgrade", fields.get("upgrade"), "websocket")
        if status == "405":
            checks.expect("handshake: 405 allows GET", fields.get("allow"), "GET")
        checks.expect("handshake: %s then closed" % name, raw.ended(), True)
        raw.close()
    # A client that says nothing is dropped once idle-ms (400) passes.
    raw = Raw(program.port)
    started = time.monotonic()
    checks.expect("handshake: a silent client is dropped", raw.ended(), True)
    checks.expect("handshake: after idle-ms", 0.3 < time.monotonic() - started < 5, True)
    raw.close()
    # RFC 6455's example key; a frame right behind the request; mixed-case
    # tokens and extra fields reach the program.
    raw = Raw(program.port)
    raw.request("GET /path?q=1 HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: WebSocket\r\nConnection: keep-alive, Upgrade\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nsec-websocket-version:13\r\nX-Spaced:  a b \t\r\n\r\n"
                .encode() + frame(1, "behind ✓".encode()))
    line, fields = raw.response()
    checks.expect("handshake: switching", line, "HTTP/1.1 101 Switching Protocols")
    checks.expect("handshake: accept", fields.get("sec-websocket-accept"), accept)
    accepted = program.result()
    checks.expect("handshake: target, peer and fields", accepted, lambda got: bool(got) and re.fullmatch(
        r'accepted \d+ "/path\?q=1" "127\.0\.0\.1:\d+" \(\("host" "127\.0\.0\.1"\) \("upgrade" "WebSocket"\) '
        r'\("connection" "keep-alive, Upgrade"\) \("sec-websocket-key" "dGhlIHNhbXBsZSBub25jZQ=="\) '
        r'\("sec-websocket-version" "13"\) \("x-spaced" "a b"\)\)', got) is not None)
    checks.expect("handshake: frame behind the request", program.result(), "message " + digest("behind ✓"))
    raw.sock.sendall(frame(8, (1000).to_bytes(2, "big")))
    checks.expect("handshake: close echoed", raw.close_code(), 1000)
    checks.expect("handshake: program sees the close", program.result(), lambda got: bool(got) and
                  re.fullmatch(r'end \(ws:closed \d+ 1000 ""\)', got) is not None)
    raw.close()
    # Ordinary clients after all of that.
    for _ in range(2):
        with connect("ws://127.0.0.1:%d/" % program.port) as ws:
            program.result()
            ws.send("after")
            checks.expect("handshake: listener still serves", program.result(), "message " + digest("after"))
        program.result()
    checks.expect("handshake: program ends", program.result(), "done")
    checks.expect("handshake: exit", program.finish(), 0)


def echo_cases(binary, directory, checks):
    program = Program(binary, directory, ECHO.replace("LIMIT", "65536").replace("IDLE", "0"), "echo")
    texts = ["hello", "πλάτων ✓ 🦄", "", "e" * 125, "e" * 126, "e" * 65535, "e" * 65536]
    with connect("ws://127.0.0.1:%d/echo?x=1" % program.port, additional_headers={"X-Test": "yes"},
                 max_size=None) as ws:
        accepted = program.result()
        checks.expect("echo: accepted", accepted, lambda got: bool(got) and '("x-test" "yes")' in got and
                      '"/echo?x=1"' in got and '("host" "127.0.0.1:%d")' % program.port in got)
        for text in texts:
            ws.send(text)
        got = [ws.recv() for _ in texts]
        checks.expect("echo: texts in order", got, texts)
        ws.send(["frag", "men", "ted ✓"])
        checks.expect("echo: fragmented", ws.recv(), "fragmented ✓")
        pong = ws.ping(b"payload")
        checks.expect("echo: ping answered", pong.wait(5), True)
        ws.close(4001, "done")
    checks.expect("echo: client close", program.result(), lambda got: bool(got) and
                  re.fullmatch(r'end \(ws:closed \d+ 4001 "done"\)', got) is not None)
    checks.expect("echo: exit", program.finish(), 0)


def push_cases(binary, directory, checks):
    sends = "".join("((io:pending $s%d) (io:submit (ws:send $c (sized %d)))) ((io:event $s%d (ws:sent $c)) (io:wait $s%d 10000))\n        "
                    % (i, n, i, i) for i, n in enumerate(LENGTHS))
    program = Program(binary, directory, PUSH.replace("SENDS", sends), "push")
    raw = Raw(program.port)
    line, fields = raw.upgrade()
    checks.expect("push: upgraded", line, "HTTP/1.1 101 Switching Protocols")
    for n in LENGTHS:
        got = raw.read_frame()
        checks.expect("push: %d bytes unmasked with their length" % n, got, (True, 1, b"p" * n))
        checks.expect("push: %d bytes in the shortest length encoding" % n, raw.header_length,
                      2 if n < 126 else 4 if n < 65536 else 10)
    got = raw.read_frame()
    checks.expect("push: server close", got, (True, 8, (4000).to_bytes(2, "big") + b"bye"))
    raw.sock.sendall(frame(8, (4000).to_bytes(2, "big") + b"bye"))
    checks.expect("push: socket closed after the echo", raw.ended(), True)
    checks.expect("push: close completes", program.result(), lambda got: bool(got) and
                  re.fullmatch(r'done \(io:event \d+ \(ws:closed \d+ 4000 "bye"\)\)', got) is not None)
    raw.close()
    checks.expect("push: exit", program.finish(), 0)


def refusal_cases(binary, directory, checks):
    """Frames a server refuses, each on its own connection, by close code."""
    big = (1 << 63).to_bytes(8, "big")
    cases = [
        ("one byte past the limit", [frame(1, b"x" * 1025)], 1009, "message too big"),
        ("binary", [frame(2, b"\x00\x01")], 1003, "binary messages are not accepted"),
        ("text not UTF-8", [frame(1, b"\xff\xfe")], 1007, "text is not UTF-8"),
        ("unmasked", [frame(1, b"hi", mask=False)], 1002, "a client's frames are masked"),
        ("reserved bits", [frame(1, b"hi", rsv=0x40)], 1002, "reserved bits are set"),
        ("unknown opcode", [frame(3, b"")], 1002, "unknown opcode"),
        ("fragmented ping", [frame(9, b"", fin=False)], 1002, "a control frame is fragmented"),
        ("long ping", [frame(9, b"p" * 126)], 1002, "a control frame carries at most 125 bytes"),
        ("stray continuation", [frame(0, b"x")], 1002, "a continuation frame outside a message"),
        ("new message inside one", [frame(1, b"a", fin=False), frame(1, b"b")], 1002,
         "a new message inside a fragmented one"),
        ("one-byte close", [frame(8, b"\x03")], 1002, "a close frame's payload is one byte"),
        ("close code 1005", [frame(8, (1005).to_bytes(2, "big"))], 1002, "invalid close code"),
        ("close code 2999", [frame(8, (2999).to_bytes(2, "big"))], 1002, "invalid close code"),
        ("close reason not UTF-8", [frame(8, (1000).to_bytes(2, "big") + b"\xff")], 1007,
         "the close reason is not UTF-8"),
        ("length out of range", [bytes([0x81, 0xff]) + big + os.urandom(4)], 1002, "frame length out of range"),
    ]
    program = Program(binary, directory, SINK.replace("LIMIT", "1024").replace("CONNECTIONS", "4")
                      .replace("IDLE", "0").replace("COUNT", str(len(cases) + 3)), "refusals")
    for name, frames, code, reason in cases:
        raw = Raw(program.port)
        raw.upgrade()
        program.result()
        raw.sock.sendall(b"".join(frames))
        checks.expect("refuse %s: close code" % name, raw.close_code(), code)
        checks.expect("refuse %s: socket closed" % name, raw.ended(), True)
        checks.expect("refuse %s: program" % name, program.result(), lambda got, code=code, reason=reason:
                      bool(got) and re.fullmatch(r'end \(ws:closed \d+ %d "%s"\)' % (code, re.escape(reason)), got)
                      is not None)
        raw.close()
    # The limit exactly, a fragmented message with a ping between fragments,
    # and a ping's payload in the pong.
    raw = Raw(program.port)
    raw.upgrade()
    program.result()
    raw.sock.sendall(frame(1, b"y" * 1024))
    checks.expect("limit exactly", program.result(), "message " + digest("y" * 1024))
    raw.sock.sendall(frame(1, b"one ", fin=False) + frame(9, b"between") + frame(0, b"two ", fin=False) +
                     frame(0, "three ✓".encode()))
    checks.expect("pong between fragments", raw.read_frame(), (True, 10, b"between"))
    checks.expect("fragments with a ping between", program.result(), "message " + digest("one two three ✓"))
    raw.sock.sendall(frame(10, b"unsolicited") + frame(1, b"after a pong"))
    checks.expect("an unsolicited pong is ignored", program.result(), "message " + digest("after a pong"))
    raw.sock.sendall(frame(8, b""))
    checks.expect("an empty close is echoed empty", raw.read_frame(), (True, 8, b""))
    checks.expect("an empty close ends with 1005", program.result(), lambda got: bool(got) and
                  re.fullmatch(r'end \(ws:closed \d+ 1005 ""\)', got) is not None)
    raw.close()
    # A client that leaves without a close frame.
    raw = Raw(program.port)
    raw.upgrade()
    program.result()
    raw.close()
    checks.expect("a client that leaves", program.result(), lambda got: bool(got) and
                  re.fullmatch(r'end \(ws:closed \d+ 1006 "the client left without a close frame"\)', got)
                  is not None)
    # Frames split into single bytes.
    raw = Raw(program.port)
    raw.upgrade()
    program.result()
    for byte in frame(1, "split ✓".encode()):
        raw.sock.sendall(bytes([byte]))
        time.sleep(0.002)
    checks.expect("a frame byte by byte", program.result(), "message " + digest("split ✓"))
    raw.sock.sendall(frame(8, (1000).to_bytes(2, "big")))
    raw.close_code()
    program.result()
    raw.close()
    checks.expect("refusals: program ends", program.result(), "done")
    checks.expect("refusals: exit", program.finish(), 0)


def keepalive_cases(binary, directory, checks):
    program = Program(binary, directory, SINK.replace("LIMIT", "1024").replace("CONNECTIONS", "4")
                      .replace("IDLE", "300").replace("COUNT", "3"), "keepalive")
    # Silent and deaf: a ping after idle-ms, the end after twice that.
    raw = Raw(program.port)
    raw.upgrade()
    program.result()
    started = time.monotonic()
    got = raw.read_frame()
    checks.expect("keepalive: a ping after silence", got[:2] if got else got, (True, 9))
    checks.expect("keepalive: not before idle-ms", time.monotonic() - started > 0.25, True)
    checks.expect("keepalive: dropped without a pong", raw.ended(), True)
    checks.expect("keepalive: after twice idle-ms", 0.5 < time.monotonic() - started < 5, True)
    checks.expect("keepalive: program sees 1006", program.result(), lambda got: bool(got) and
                  re.fullmatch(r'end \(ws:closed \d+ 1006 "the client did not answer a ping"\)', got) is not None)
    raw.close()
    # A client that answers pings stays for more than four idle periods.
    with connect("ws://127.0.0.1:%d/" % program.port) as ws:
        program.result()
        time.sleep(1.4)
        ws.send("still here")
        checks.expect("keepalive: an answering client stays", program.result(), "message " + digest("still here"))
    program.result()
    # So does one that keeps sending, without answering.
    raw = Raw(program.port)
    raw.upgrade()
    program.result()
    for i in range(8):
        raw.sock.sendall(frame(1, b"tick"))
        time.sleep(0.2)
    for i in range(8):
        checks.expect("keepalive: a talking client stays (%d)" % i, program.result(), "message " + digest("tick"))
    raw.sock.sendall(frame(8, (1000).to_bytes(2, "big")))
    program.result()
    raw.close()
    checks.expect("keepalive: program ends", program.result(), "done")
    checks.expect("keepalive: exit", program.finish(), 0)


def close_wait_cases(binary, directory, checks):
    program = Program(binary, directory, CLOSE_UNANSWERED, "close-wait")
    raw = Raw(program.port)
    raw.upgrade()
    program.result()
    checks.expect("close wait: close frame sent", raw.read_frame(), (True, 8, (1000).to_bytes(2, "big") + b"now"))
    started = time.monotonic()
    result = program.result(20)
    checks.expect("close wait: completes", result, lambda got: bool(got) and
                  re.fullmatch(r'\(io:event \d+ \(ws:closed \d+ 1000 "now"\)\)', got) is not None)
    checks.expect("close wait: after about five seconds", 4 < time.monotonic() - started < 9, True)
    checks.expect("close wait: socket closed", raw.ended(), True)
    raw.close()
    checks.expect("close wait: exit", program.finish(), 0)


def capacity_cases(binary, directory, checks):
    program = Program(binary, directory, SINK.replace("LIMIT", "1024").replace("CONNECTIONS", "2")
                      .replace("IDLE", "0").replace("COUNT", "3"), "capacity")
    first = connect("ws://127.0.0.1:%d/" % program.port)
    program.result()
    second = connect("ws://127.0.0.1:%d/" % program.port)
    # The third waits in the kernel's queue until a connection ends.
    opened = queue.Queue()
    def third_client():
        try:
            opened.put(connect("ws://127.0.0.1:%d/" % program.port, open_timeout=20))
        except Exception as error:
            opened.put(error)
    threading.Thread(target=third_client, daemon=True).start()
    # Traffic on an open connection wakes the runtime, which pumps the
    # listener: it must still accept nobody.
    time.sleep(0.3)
    for i in range(3):
        first.send("busy %d" % i)
        checks.expect("capacity: traffic while full (%d)" % i, program.result(), "message " + digest("busy %d" % i))
    try:
        early = opened.get(timeout=1.5)
    except queue.Empty:
        early = None
    checks.expect("capacity: the third client waits", early, None)
    first.close()
    checks.expect("capacity: first ends", program.result(), lambda got: bool(got) and got.startswith("end "))
    checks.expect("capacity: second accepted", program.result(), lambda got: bool(got) and got.startswith("accepted"))
    try:
        third = early or opened.get(timeout=10)
    except queue.Empty:
        third = None
    checks.expect("capacity: the third is served once one left", hasattr(third, "send"), True)
    second.send("second")
    checks.expect("capacity: second still served", program.result(), "message " + digest("second"))
    second.close()
    program.result()
    if hasattr(third, "send"):
        program.result()
        third.send("third")
        checks.expect("capacity: third served", program.result(), "message " + digest("third"))
        third.close()
        program.result()
    checks.expect("capacity: program ends", program.result(), "done")
    checks.expect("capacity: exit", program.finish(), 0)


def accept_cases(binary, directory, checks):
    program = Program(binary, directory, CANCEL, "cancel")
    with connect("ws://127.0.0.1:%d/kept" % program.port) as ws:
        ws.send("kept")
        checks.expect("cancel: the next accept gets the connection", program.result(), lambda got: bool(got) and
                      re.fullmatch(r'true "/kept" \(ws:message \d+ "kept"\)', got) is not None)
    checks.expect("cancel: exit", program.finish(), 0)
    program = Program(binary, directory, STOP, "stop")
    ws = connect("ws://127.0.0.1:%d/" % program.port)
    checks.expect("stop: stopped", program.result(), "stopped")
    refused = False
    try:
        connect("ws://127.0.0.1:%d/" % program.port, open_timeout=3).close()
    except (ConnectionRefusedError, OSError):
        refused = True
    checks.expect("stop: new clients refused", refused, True)
    ws.send("after the stop")
    checks.expect("stop: accepted connection works", program.result(), lambda got: bool(got) and re.fullmatch(
        r'\(ws:stopped \d+\) \(ws:stopped \d+\) refused \(ws:message \d+ "after the stop"\)', got) is not None)
    ws.close()
    checks.expect("stop: exit", program.finish(), 0)


def failure_cases(binary, directory, checks):
    busy = socket.socket()
    busy.bind(("127.0.0.1", 0))
    busy.listen()
    program = Program(binary, directory, FAILURES.replace("BUSY", str(busy.getsockname()[1])), "failures")
    checks.expect("listen: port in use", program.result(), lambda got: bool(got) and
                  re.fullmatch(r'\(ws:listen-failed \d+ "Address already in use"\)', got) is not None)
    checks.expect("listen: unknown host", program.result(), "listen-failed")
    for name in ("port 70000", "no message bytes", "no connections", "accept on no listener",
                 "stop on no listener"):
        checks.expect("listen: " + name + " refused", program.result(), "refused")
    checks.expect("capabilities list ws-server", program.result(), lambda got: bool(got) and
                  "ws-server" in got.strip("()").split())
    checks.expect("failures: exit", program.finish(), 0)
    busy.close()


def loopback_cases(binary, directory, checks):
    program = Program(binary, directory, LOOPBACK, "loopback")
    checks.expect("loopback: both ways", program.result(), lambda got: bool(got) and re.fullmatch(
        r'"/loop" \(ws:message \d+ "from the client ✓"\) \(ws:message \d+ "from the server ✓"\) '
        r'\(ws:closed \d+ 4002 "loop done"\) \(ws:closed \d+ 4002 "loop done"\)', got) is not None)
    checks.expect("loopback: exit", program.finish(), 0)


def flood_text(i):
    return "%04d " % i + "f" * 200


def flood_cases(binary, directory, checks):
    program = Program(binary, directory, FLOOD.replace("COUNT", str(FLOOD_COUNT)), "flood")
    with connect("ws://127.0.0.1:%d/" % program.port) as ws:
        for i in range(FLOOD_COUNT):
            ws.send(flood_text(i))
        want = ""
        for i in range(FLOOD_COUNT):
            want = hashlib.sha256((want + flood_text(i)).encode()).hexdigest()
        checks.expect("flood: all in order", program.result(60), '"%s"' % want)
    checks.expect("flood: exit", program.finish(), 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("binary", type=Path)
    arguments = parser.parse_args()
    if connect is None:
        print("io websocket server: skipped, this Python has no websockets package")
        return 0
    binary = arguments.binary.resolve()
    checks = Checks()
    with tempfile.TemporaryDirectory() as directory:
        for group in (handshake_cases, echo_cases, push_cases, refusal_cases, keepalive_cases, close_wait_cases,
                      capacity_cases, accept_cases, failure_cases, loopback_cases, flood_cases):
            try:
                group(binary, directory, checks)
            except Exception:  # a group that breaks counts once and the rest still run
                checks.total += 1
                checks.failures.append((group.__name__, traceback.format_exc(limit=-3), "no exception"))
    for name, got, want in checks.failures[:30]:
        print("FAIL %s:\n  got  %.900r\n  want %.300r" % (name, got, want))
    print("io websocket server: %d checks, %d failures" % (checks.total, len(checks.failures)))
    return 1 if checks.failures else 0


if __name__ == "__main__":
    sys.exit(main())
