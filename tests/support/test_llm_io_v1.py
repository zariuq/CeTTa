#!/usr/bin/env python3
"""Buffered LLM requests against a local peer; no accounts or external traffic."""
import http.server
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]


class Peer(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        request = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        with self.server.lock:
            self.server.requests.append((self.path, request))
        model = request["model"]
        if model in ("slow", "timeout", "cancel"):
            time.sleep(.2)
        wire = {"id": model, "choices": [{"index": 0, "message": {
            "role": "assistant", "content": model}, "finish_reason": "stop"}]}
        if model == "malformed":
            body = b'{"choices":[],"choices":[]}'
        elif model == "large":
            body = b"x" * 4096
        else:
            body = json.dumps(wire).encode()
        self.send_response(429 if model == "quota" else 200)
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass


@unittest.skipUnless(os.environ.get("CETTA_BIN"), "set CETTA_BIN to an HTTP-enabled build")
class OwnedRequests(unittest.TestCase):
    def setUp(self):
        self.peer = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Peer)
        self.peer.requests, self.peer.lock = [], threading.Lock()
        threading.Thread(target=self.peer.serve_forever, daemon=True).start()

    def tearDown(self):
        self.peer.shutdown()
        self.peer.server_close()

    def submit(self, model, timeout=2000, cap=8192):
        request = json.dumps({"model": model, "max_tokens": 42, "messages": [
            {"role": "user", "content": "fixture"}]})
        base = f"http://127.0.0.1:{self.peer.server_port}/v1"
        return f'(llm:v1:submit session {model} "chat-completions" {json.dumps(base)} () {json.dumps(request)} {timeout} {cap})'

    def run_guest(self, lang, forms):
        source = '''!(import! &self llm:io)
(: fixture:await (-> Atom %Undefined%))
(= (fixture:await $phase)
   (case $phase
     (((llm:v1:pending $epoch $correlation $protocol $id)
       (let $next (llm:v1:wait $phase 500) (fixture:await $next)))
      ($terminal $phase))))
(: fixture:show (-> Atom %Undefined%))
(= (fixture:show $phase)
   (case $phase
     (((llm:v1:completed $epoch $correlation $result) (llm:v1:render $result))
      ((llm:v1:failed $epoch $correlation (http-status $status) $certainty) "HTTP-ERROR")
      ((llm:v1:failed $epoch $correlation $code $certainty) (str:concat "ERROR:" (str:symbol->string $code)))
      ((llm:v1:cancelled $epoch $correlation $certainty) "CANCELLED")
      ($other "INVALID-PHASE"))))
'''
        with tempfile.TemporaryDirectory(prefix="cetta-llm-io-") as directory:
            entry = Path(directory) / "fixture.metta"
            entry.write_text(source + forms)
            result = subprocess.run([os.environ["CETTA_BIN"], "--lang", lang,
                "--import-mode", "ancestor-walk", str(entry)], cwd=ROOT,
                capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr[-1200:])
            outputs = []
            for line in result.stdout.splitlines():
                if line.startswith(('"', '["')):
                    value = json.loads(line)
                    outputs.append(value[0] if isinstance(value, list) else value)
                elif line.startswith(("{", "ERROR:")) or line in ("CANCELLED", "FOREIGN", "HTTP-ERROR"):
                    outputs.append(line)
            return outputs, result.stdout

    def test_owner_wait_preserves_foreign_completion_and_terminal_is_stable(self):
        for lang in ("he", "petta"):
            before = len(self.peer.requests)
            forms = f'''!(let* (($fast {self.submit("fast")}) ($slow {self.submit("slow")})
  ($done (fixture:await $slow)) ($again (llm:v1:wait $done 0))
  ($other (fixture:await $fast)))
  (progn (println! (fixture:show $done)) (println! (fixture:show $again))
         (println! (fixture:show $other))))
'''
            outputs, raw = self.run_guest(lang, forms)
            self.assertEqual(len(outputs), 3, raw[-1200:])
            self.assertEqual([json.loads(x)["response_id"] for x in outputs], ["slow", "slow", "fast"])
            self.assertEqual(len(self.peer.requests) - before, 2)

    def test_header_json_bridge_reifies_only_literal_header_tags(self):
        value = json.dumps([["http:header", "Content-Type", "application/json"],
                            ["http:credential", "FIXTURE"]])
        forms = f'''!(case (llm:v1:headers-json {json.dumps(value)})
  (((llm:v1:ok ((http:header $name $value) (http:credential $alias)))
      (if (and (== $name "Content-Type")
               (and (== $value "application/json") (== $alias "FIXTURE"))) "HEADERS" "WRONG"))
   ($other "WRONG")))
'''
        for value in ('[["println!", "SHOULD-NOT-RUN"]]', '[["http:credential", ""]]',
                      '[["http:header", "name", 3]]', '{"items":[]}'):
            forms += f'''!(case (llm:v1:headers-json {json.dumps(value)})
  (((llm:v1:error $code) "REJECTED") ($other "WRONG")))
'''
        for lang in ("he", "petta"):
            outputs, raw = self.run_guest(lang, forms)
            self.assertEqual(outputs, ["HEADERS", "REJECTED", "REJECTED", "REJECTED", "REJECTED"], raw[-1200:])
            self.assertNotIn("SHOULD-NOT-RUN", raw)
        self.assertEqual(self.peer.requests, [])

    def test_failures_are_bounded_and_never_implicitly_retried(self):
        for lang in ("he", "petta"):
            before = len(self.peer.requests)
            forms = "\n".join(f'!(let* (($pending {self.submit(model, timeout, cap)}) '
                '($done (fixture:await $pending))) (fixture:show $done))'
                for model, timeout, cap in (("quota", 2000, 8192), ("malformed", 2000, 8192),
                                           ("large", 2000, 128), ("timeout", 30, 8192)))
            outputs, raw = self.run_guest(lang, forms)
            self.assertEqual(outputs, ["HTTP-ERROR", "ERROR:invalid-or-ambiguous-json",
                                      "ERROR:transport-error", "ERROR:transport-error"], raw[-1200:])
            self.assertEqual(len(self.peer.requests) - before, 4)

    def test_cancel_and_foreign_observations_cannot_reopen_a_terminal_phase(self):
        for lang in ("he", "petta"):
            forms = f'''!(let* (($pending {self.submit("cancel")}) ($done (llm:v1:cancel $pending))
  ($again (llm:v1:wait $done 0)) ($last (llm:v1:observe $done (io:event 999 (http:response 200 "private-fixture")))))
  (progn (println! (fixture:show $done)) (println! (fixture:show $again))
         (println! (fixture:show $last))))
!(let* (($pending (llm:v1:pending session turn "chat-completions" 999))
        ($observed (llm:v1:observe $pending (io:event 998 (http:response 200 "foreign-fixture")))))
   (case $observed
     (((llm:v1:foreign $same $event) "FOREIGN") ($other "WRONG"))))
'''
            outputs, raw = self.run_guest(lang, forms)
            self.assertEqual(outputs, ["CANCELLED", "CANCELLED", "CANCELLED", "FOREIGN"], raw[-1200:])
            self.assertNotIn("private-fixture", raw)


if __name__ == "__main__":
    unittest.main()
