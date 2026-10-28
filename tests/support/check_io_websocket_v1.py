#!/usr/bin/env python3
"""WebSocket requests of the io runtime against a local server.

The server (Python's websockets package, run with a Python that has it) has
one endpoint per behavior.  Each case is one MeTTa program run in PeTTa; its
printed result and what the server observed are both checked:

- echo of ASCII, multilingual and empty text; frames queued before the
  handshake completes go out after it, and frames queued together keep
  their order;
- messages the server pushes arrive in order, also fragmented ones;
- a message of max-message-bytes arrives whole; one byte more closes with
  1009, a binary message with 1003, text that is not UTF-8 with 1007;
- closes from either side carry their code and reason, and ws:close releases
  the connection, so a later request naming it is refused;
- a receive cancelled after a message was assigned to it returns the
  message, which the next receive gets;
- no listener answers ws:failed with curl's code; pings are answered;
- a connection outlives its connect timeout;
- io:wait-ready over two connections reports only the one with a message;
- 400 messages sent while the program sleeps all arrive, in order, through a
  queue limited to 16 KiB, so reading paused and resumed without loss.
"""
import argparse
import asyncio
import hashlib
import re
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

try:
    from websockets.asyncio.server import serve
except ImportError:  # pragma: no cover - reported as a skip below
    serve = None

FLOOD_COUNT = 400


def flood_text(i):
    return "%04d " % i + "f" * 200


class Server:
    def __init__(self):
        self.observed = {}
        self.loop = asyncio.new_event_loop()
        self.ready = threading.Event()
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()
        self.ready.wait(10)

    def _run(self):
        asyncio.set_event_loop(self.loop)
        self.loop.run_until_complete(self._start())
        self.ready.set()
        self.loop.run_forever()

    async def _start(self):
        self.server = await serve(self.handler, "127.0.0.1", 0, max_size=None, ping_interval=None)
        self.port = self.server.sockets[0].getsockname()[1]

    def note(self, path, **facts):
        self.observed.setdefault(path, {}).update(facts)

    async def handler(self, ws):
        path = ws.request.path
        try:
            if path == "/echo":
                received = []
                async for message in ws:
                    received.append(message)
                    await ws.send(message)
                self.note(path, received=received)
            elif path == "/push":
                for text in ("one", "two", "three"):
                    await ws.send(text)
                await ws.send(["frag", "men", "ted"])
                await ws.wait_closed()
            elif path.startswith("/size/"):
                await ws.send("s" * int(path.split("/")[2]))
                await ws.wait_closed()
            elif path == "/binary":
                await ws.send(b"\x00\x01")
                await ws.wait_closed()
            elif path == "/bad-utf8":
                async with ws.send_context():
                    ws.protocol.send_text(b"\xff\xfe")
                await ws.wait_closed()
            elif path == "/close-4000":
                await ws.close(4000, "bye")
            elif path == "/ping":
                pong = await ws.ping()
                await asyncio.wait_for(pong, 5)
                self.note(path, pong=True)
                await ws.send("after ping")
                await ws.wait_closed()
            elif path == "/flood":
                for i in range(FLOOD_COUNT):
                    await ws.send(flood_text(i))
                await ws.wait_closed()
            elif path == "/quiet":
                await ws.wait_closed()
            elif path == "/late":
                await asyncio.sleep(1.5)
                await ws.send("still here")
                await ws.wait_closed()
        finally:
            self.note(path, close_code=ws.close_code, close_reason=ws.close_reason)

    def stop(self):
        self.server.close()
        self.loop.call_soon_threadsafe(self.loop.stop)


PROGRAM_HEAD = "!(import! &self (library io))\n!(import! &self (library str))\n"

# Each case: name, program body (PORT and URL stand for the server), the
# printed RESULT lines wanted, and server observations wanted.
def cases(url):
    def connect(path, limit=1048576, timeout=5000):
        return '((io:pending $c) (io:submit (ws:connect "%s%s" %d %d)))' % (url, path, timeout, limit)
    wait = "(io:wait %s 5000)"
    return [
        ("echo",
         "!(let* (%s ($o %s) ((io:pending $a) (io:submit (ws:send $c \"hello\"))) ($sa %s)"
         " ((io:pending $b) (io:submit (ws:send $c \"πλάτων ✓ 🦄\"))) ($sb %s)"
         " ((io:pending $e) (io:submit (ws:send $c \"\"))) ($se %s)"
         " ((io:pending $r1) (io:submit (ws:receive $c))) ($m1 %s)"
         " ((io:pending $r2) (io:submit (ws:receive $c))) ($m2 %s)"
         " ((io:pending $r3) (io:submit (ws:receive $c))) ($m3 %s)"
         " ((io:pending $x) (io:submit (ws:close $c 1000 \"done\"))) ($cx %s))"
         " (println! (RESULT $o $sa $m1 $m2 $m3 $cx)))"
         % (connect("/echo"), wait % "$c", wait % "$a", wait % "$b", wait % "$e", wait % "$r1",
            wait % "$r2", wait % "$r3", wait % "$x"),
         ['(RESULT (io:event C (ws:connected C)) (io:event A (ws:sent C)) (io:event R (ws:message C "hello"))'
          ' (io:event R (ws:message C "πλάτων ✓ 🦄")) (io:event R (ws:message C "")) (io:event X (ws:closed C 1000 "done")))'],
         {"/echo": {"received": ["hello", "πλάτων ✓ 🦄", ""], "close_code": 1000, "close_reason": "done"}}),
        ("send before the handshake",
         "!(let* (%s ((io:pending $a) (io:submit (ws:send $c \"early\"))) ($o %s) ($sa %s)"
         " ((io:pending $r) (io:submit (ws:receive $c))) ($m %s)"
         " ((io:pending $x) (io:submit (ws:close $c 1000 \"\"))) ($cx %s))"
         " (println! (RESULT $o $sa $m)))"
         % (connect("/echo"), wait % "$c", wait % "$a", wait % "$r", wait % "$x"),
         ['(RESULT (io:event C (ws:connected C)) (io:event A (ws:sent C)) (io:event R (ws:message C "early")))'],
         {}),
        ("frames queued together keep their order",
         "!(let* (%s ((io:pending $a) (io:submit (ws:send $c \"a\"))) ((io:pending $b) (io:submit (ws:send $c \"b\")))"
         " ((io:pending $d) (io:submit (ws:send $c \"c\"))) ($o %s) ($sa %s) ($sb %s) ($sd %s)"
         " ((io:pending $r1) (io:submit (ws:receive $c))) ($m1 %s)"
         " ((io:pending $r2) (io:submit (ws:receive $c))) ($m2 %s)"
         " ((io:pending $r3) (io:submit (ws:receive $c))) ($m3 %s)"
         " ((io:pending $x) (io:submit (ws:close $c 1000 \"\"))) ($cx %s))"
         " (println! (RESULT $m1 $m2 $m3)))"
         % (connect("/echo"), wait % "$c", wait % "$a", wait % "$b", wait % "$d", wait % "$r1", wait % "$r2",
            wait % "$r3", wait % "$x"),
         ['(RESULT (io:event R (ws:message C "a")) (io:event R (ws:message C "b")) (io:event R (ws:message C "c")))'],
         {}),
        ("a cancelled receive gives its message back",
         "!(let* (%s ($o %s) ((io:pending $r1) (io:submit (ws:receive $c)))"
         " ($ready (io:wait-ready (io:ids $r1) 5000)) ($cancelled (io:cancel $r1))"
         " ((io:pending $r2) (io:submit (ws:receive $c))) ($m %s)"
         " ((io:pending $x) (io:submit (ws:close $c 1000 \"\"))) ($cx %s))"
         " (println! (RESULT (== $ready (io:ready ($r1))) $m)))"
         % (connect("/push"), wait % "$c", wait % "$r2", wait % "$x"),
         ['(RESULT true (io:event R (ws:message C "one")))'],
         {}),
        ("pushed and fragmented messages",
         "!(let* (%s ($o %s) ((io:pending $r1) (io:submit (ws:receive $c))) ($m1 %s)"
         " ((io:pending $r2) (io:submit (ws:receive $c))) ($m2 %s)"
         " ((io:pending $r3) (io:submit (ws:receive $c))) ($m3 %s)"
         " ((io:pending $r4) (io:submit (ws:receive $c))) ($m4 %s)"
         " ((io:pending $x) (io:submit (ws:close $c 1000 \"\"))) ($cx %s))"
         " (println! (RESULT $m1 $m2 $m3 $m4)))"
         % (connect("/push"), wait % "$c", wait % "$r1", wait % "$r2", wait % "$r3", wait % "$r4", wait % "$x"),
         ['(RESULT (io:event R (ws:message C "one")) (io:event R (ws:message C "two"))'
          ' (io:event R (ws:message C "three")) (io:event R (ws:message C "fragmented")))'],
         {}),
        ("a message of the limit",
         "!(let* (%s ($o %s) ((io:pending $r) (io:submit (ws:receive $c)))"
         " ((io:event $r (ws:message $c $text)) %s)"
         " ((io:pending $x) (io:submit (ws:close $c 1000 \"\"))) ($cx %s))"
         " (println! (RESULT (str:byte-length $text) (str:sha256 $text))))"
         % (connect("/size/65536", 65536), wait % "$c", wait % "$r", wait % "$x"),
         ["(RESULT 65536 \"%s\")" % hashlib.sha256(b"s" * 65536).hexdigest()],
         {}),
        ("a message past the limit",
         "!(let* (%s ($o %s) ((io:pending $r) (io:submit (ws:receive $c))) ($m %s))"
         " (println! (RESULT $m)))" % (connect("/size/65537", 65536), wait % "$c", wait % "$r"),
         ['(RESULT (io:event R (ws:closed C 1009 "message too big")))'],
         {"/size/65537": {"close_code": 1009}}),
        ("a binary message",
         "!(let* (%s ($o %s) ((io:pending $r) (io:submit (ws:receive $c))) ($m %s))"
         " (println! (RESULT $m)))" % (connect("/binary"), wait % "$c", wait % "$r"),
         ['(RESULT (io:event R (ws:closed C 1003 "binary messages are not accepted")))'],
         {"/binary": {"close_code": 1003}}),
        ("text that is not UTF-8",
         "!(let* (%s ($o %s) ((io:pending $r) (io:submit (ws:receive $c))) ($m %s))"
         " (println! (RESULT $m)))" % (connect("/bad-utf8"), wait % "$c", wait % "$r"),
         ['(RESULT (io:event R (ws:closed C 1007 "text is not UTF-8")))'],
         {}),
        ("the server closes",
         "!(let* (%s ($o %s) ((io:pending $r) (io:submit (ws:receive $c))) ($m %s)"
         " ((io:pending $s) (io:submit (ws:send $c \"late\"))) ($ms %s)"
         " ((io:pending $x) (io:submit (ws:close $c 1000 \"\"))) ($cx %s)"
         " ($again (io:submit (ws:receive $c))))"
         " (println! (RESULT $m $ms $cx (case $again (((Error $call $why) refused) ($other $other))))))"
         % (connect("/close-4000"), wait % "$c", wait % "$r", wait % "$s", wait % "$x"),
         ['(RESULT (io:event R (ws:closed C 4000 "bye")) (io:event S (ws:closed C 4000 "bye"))'
          ' (io:event X (ws:closed C 4000 "bye")) refused)'],
         {}),
        ("no listener",
         "!(let* (((io:pending $c) (io:submit (ws:connect \"ws://127.0.0.1:1/\" 2000 1024)))"
         " ((io:event $c (ws:failed $c $code $text)) %s))"
         " (println! (RESULT $code)))" % (wait % "$c"),
         ['(RESULT 7)'],
         {}),
        ("pings are answered",
         "!(let* (%s ($o %s) ((io:pending $r) (io:submit (ws:receive $c))) ($m %s)"
         " ((io:pending $x) (io:submit (ws:close $c 1000 \"\"))) ($cx %s))"
         " (println! (RESULT $m)))" % (connect("/ping"), wait % "$c", wait % "$r", wait % "$x"),
         ['(RESULT (io:event R (ws:message C "after ping")))'],
         {"/ping": {"pong": True}}),
        ("a connection outlives its connect timeout",
         "!(let* (%s ($o %s) ((io:pending $r) (io:submit (ws:receive $c))) ($m %s)"
         " ((io:pending $x) (io:submit (ws:close $c 1000 \"\"))) ($cx %s))"
         " (println! (RESULT $m)))" % (connect("/late", 1024, 500), wait % "$c", wait % "$r", wait % "$x"),
         ['(RESULT (io:event R (ws:message C "still here")))'],
         {}),
        ("readiness over two connections",
         "!(let* (((io:pending $q) (io:submit (ws:connect \"%s/quiet\" 5000 1024))) ($oq %s)"
         " ((io:pending $p) (io:submit (ws:connect \"%s/push\" 5000 1024))) ($op %s)"
         " ((io:pending $rq) (io:submit (ws:receive $q))) ((io:pending $rp) (io:submit (ws:receive $p)))"
         " ($ready (io:wait-ready (io:ids $rq $rp) 5000)) ($m %s)"
         " ((io:pending $xq) (io:submit (ws:close $q 1000 \"\"))) ($cq %s)"
         " ((io:pending $xp) (io:submit (ws:close $p 1000 \"\"))) ($cp %s))"
         " (println! (RESULT (== $ready (io:ready ($rp))) $m $cq)))"
         % (url, wait % "$q", url, wait % "$p", wait % "$rp", wait % "$xq", wait % "$xp"),
         ['(RESULT true (io:event R (ws:message C "one")) (io:event X (ws:closed C 1000 "")))'],
         {}),
        ("a flood through a small queue",
         "(= (drain $c 0 $digest) $digest)\n"
         "(= (drain $c $n $digest)\n"
         "   (let* (((io:pending $r) (io:submit (ws:receive $c))) ((io:event $r (ws:message $c $text)) (io:wait $r 5000)))\n"
         "     (drain $c (- $n 1) (str:sha256 (str:concat $digest $text)))))\n"
         "!(let* (%s ($o %s) ((io:pending $t) (io:submit (timer:after 500))) ($slept %s)"
         " ($digest (drain $c %d \"\"))"
         " ((io:pending $x) (io:submit (ws:close $c 1000 \"\"))) ($cx %s))"
         " (println! (RESULT $digest)))"
         % (connect("/flood", 16384), wait % "$c", wait % "$t", FLOOD_COUNT, wait % "$x"),
         ["(RESULT \"%s\")" % flood_digest()],
         {}),
    ]


def flood_digest():
    digest = ""
    for i in range(FLOOD_COUNT):
        digest = hashlib.sha256((digest + flood_text(i)).encode()).hexdigest()
    return digest


def normalize(line):
    """Request and connection IDs differ by run; both sides name them '?'."""
    return re.sub(r"\(io:event \d+ \((ws:[a-z]+) \d+", lambda m: "(io:event ? (%s ?" % m.group(1), line)


def shape(text):
    """The wanted lines name a request and a connection by letters."""
    return re.sub(r"\(io:event [A-Z] \((ws:[a-z]+) [A-Z]", lambda m: "(io:event ? (%s ?" % m.group(1), text)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("binary", type=Path)
    arguments = parser.parse_args()
    if serve is None:
        print("io websocket: skipped, this Python has no websockets package")
        return 0
    server = Server()
    url = "ws://127.0.0.1:%d" % server.port
    failures, total = [], 0
    with tempfile.TemporaryDirectory() as directory:
        for name, body, want, observed in cases(url):
            program = Path(directory) / "case.metta"
            program.write_text(PROGRAM_HEAD + body + "\n", encoding="utf-8")
            result = subprocess.run([str(arguments.binary.resolve()), "--lang", "petta", str(program)],
                                    capture_output=True, encoding="utf-8", errors="replace", timeout=120)
            got = [normalize(line) for line in result.stdout.split("\n") if line.startswith("(RESULT")]
            total += 1
            if result.returncode or got != [shape(w) for w in want]:
                failures.append((name, result.returncode, got, [shape(w) for w in want], result.stderr[-600:]))
            for path, facts in observed.items():
                total += 1
                for _ in range(40):
                    if "close_code" in server.observed.get(path, {}):
                        break
                    threading.Event().wait(0.05)
                seen = server.observed.get(path, {})
                if any(seen.get(key) != value for key, value in facts.items()):
                    failures.append((name + " (server)", 0, seen, facts, ""))
    server.stop()
    for name, code, got, want, err in failures:
        print("FAIL %s (exit %s):\n  got  %s\n  want %s\n  %s" % (name, code, got, want, err.strip()[:300]))
    print("io websocket: %d checks, %d failures" % (total, len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
