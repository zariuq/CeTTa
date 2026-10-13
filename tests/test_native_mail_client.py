#!/usr/bin/env python3
"""Exercise the MeTTa client in separate CeTTa processes; no Python mailbox host."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
BIN = str(Path(os.environ.get("CETTA_BIN", ROOT / "cetta")).resolve())
SERVICE = os.environ.get("CETTA_SPACE_SERVICE", str(ROOT / "runtime/cetta-space-service"))


class NativeMail(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.sock = str(self.root / "s.sock")
        self.start()

    def start(self):
        self.proc = subprocess.Popen([SERVICE, "--socket", self.sock, "--data", str(self.root / "data")],
                                     stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        for _ in range(500):
            if Path(self.sock).exists():
                return
            if self.proc.poll() is not None:
                self.fail(self.proc.stderr.read().decode())
            time.sleep(.01)
        self.fail("service did not start")

    def stop(self):
        self.proc.terminate()
        self.proc.communicate(timeout=10)

    def tearDown(self):
        self.stop()
        self.temp.cleanup()

    def run_metta(self, code, lang="petta"):
        p = self.root / (str(time.time_ns()) + ".metta")
        p.write_text("!(import! &self mailbox)\n" + code)
        run = subprocess.run([BIN, "--lang", lang, "--quiet", str(p)], cwd=ROOT,
                             text=True, capture_output=True, timeout=45)
        self.assertEqual(run.returncode, 0, run.stderr)
        return run.stdout

    def call(self, op, *args, lang="petta"):
        q = lambda x: json.dumps(x, ensure_ascii=False)
        return self.run_metta("!(mailbox:" + op + " " + " ".join(map(q, (self.sock, "test-mail", *args))) + ")", lang)

    def test_exchange_and_owner_isolation_in_both_dialects(self):
        for lang in ("he", "petta"):
            ident = "hello-" + lang
            self.assertIn("accepted", self.call("send", ident, "reader", "writer", "λ 🌸\nquoted \" text", lang=lang))
            self.assertIn("mailbox:idle", self.call("next", "other", lang=lang))
            received = self.call("next", "reader", lang=lang)
            self.assertIn(ident, received)
            self.assertIn("λ 🌸", received)
            self.assertIn("settled", self.call("ack", "reader", ident, lang=lang))
            self.assertIn("mailbox:idle", self.call("next", "reader", lang=lang))

    def test_duplicate_and_conflict_after_service_restart(self):
        self.call("send", "stable", "reader", "writer", "original")
        self.stop()
        self.start()
        self.assertIn("duplicate", self.call("send", "stable", "reader", "writer", "original"))
        self.assertIn("identity-conflict", self.call("send", "stable", "reader", "writer", "changed"))
        self.assertIn("original", self.call("next", "reader"))
        self.call("ack", "reader", "stable")
        self.assertIn("mailbox:idle", self.call("next", "reader"))

    def test_unacknowledged_receive_survives_both_process_restarts(self):
        self.call("send", "restart", "reader", "writer", "retain me")
        first = self.call("next", "reader")
        self.stop()
        self.start()
        self.assertEqual(first, self.call("next", "reader"))
        self.assertIn("settled", self.call("ack", "reader", "restart"))
        self.assertIn("duplicate", self.call("ack", "reader", "restart"))
        self.assertIn("mailbox:idle", self.call("next", "reader"))

    def test_concurrent_senders(self):
        self.call("next", "reader")  # create the shared store before racing
        with ThreadPoolExecutor(max_workers=4) as pool:
            outputs = list(pool.map(lambda i: self.call("send", "race-" + str(i), "reader", "writer", str(i)), range(12)))
        self.assertTrue(all("accepted" in s for s in outputs), outputs)
        for _ in range(12):
            output = self.call("next", "reader")
            import re
            ident = re.search(r'mailbox:delivery "([^"]+)"', output).group(1)
            self.call("ack", "reader", ident)
        self.assertIn("mailbox:idle", self.call("next", "reader"))

    def test_large_body_is_not_cut(self):
        body = "λ" * 120000 + " END"
        self.assertIn("accepted", self.call("send", "large", "reader", "writer", body))
        self.assertIn(body, self.call("next", "reader"))

    def test_native_watch_wakes_for_a_send(self):
        self.call("next", "reader")
        q = json.dumps(self.sock)
        code = ('!(let $space (mailbox:open ' + q + ' "test-mail") '
                '(mailbox:watch ' + q + ' $space 30000))')
        with ThreadPoolExecutor(max_workers=1) as pool:
            future = pool.submit(self.run_metta, code)
            time.sleep(.2)
            started = time.monotonic()
            self.call("send", "wake", "reader", "writer", "wake now")
            self.assertIn("changed", future.result(timeout=5))
            self.assertLess(time.monotonic() - started, 5)


if __name__ == "__main__":
    unittest.main()
