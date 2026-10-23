"""Persistent activity mailboxes against the actual native host and CWP1."""
import json
import os
from pathlib import Path
import queue
import socket
from support.durable_journal_fixture import records
import subprocess
import threading
import time
import unittest
import tempfile


class Service:
    def __init__(self, directory, capacity=4, policy=None, quantum=1, work_limit=100000,
                 cleanup_quantum=None, allow_atomic_cleanup=False):
        self.directory = Path(directory)
        self.path = self.directory / "worker.sock"
        self.listener = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        self.listener.bind(str(self.path)); self.listener.listen(16)
        self.capacity = capacity
        self.planning = [] if policy is None else [str(policy), str(quantum), str(work_limit)]
        if cleanup_quantum is not None:
            assert policy is not None
            self.planning += [str(cleanup_quantum), str(int(allow_atomic_cleanup))]
        self.process = None
        self.lines = []
        self.start()

    def start(self):
        self.events = queue.Queue()
        self.process = subprocess.Popen([os.environ["CETTA_ACTIVITY_SERVICE"],
            str(self.directory / "journal.cdr"), str(self.listener.fileno()),
            "activities", str(self.capacity)] + self.planning, pass_fds=[self.listener.fileno()],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, bufsize=1)
        def read():
            for line in self.process.stdout:
                self.lines.append(line.rstrip()); self.events.put(line.rstrip())
        self.reader = threading.Thread(target=read, daemon=True); self.reader.start()
        self.wait(lambda line: line == "ready")

    def wait(self, predicate, seconds=5):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            try:
                line = self.events.get(timeout=min(.1, max(.001, deadline - time.monotonic())))
            except queue.Empty:
                if self.process.poll() is not None:
                    raise AssertionError("native service exited: " + self.process.stderr.read())
                continue
            if predicate(line): return line
        raise AssertionError("missing event; seen " + repr(self.lines[-10:]))

    def record(self, kind, identity, owner=None, generation=1, body="fixture", due=0, period=0):
        fields = [kind, identity, owner or identity, str(generation)]
        if kind == "timer": fields += [str(due), str(period)]
        self.process.stdin.write("\t".join(fields + [body]) + "\n"); self.process.stdin.flush()
        return self.wait(lambda line: line.startswith("intake " + identity + " "))

    def rpc(self, code, identity="", body=""):
        name = identity.encode("ascii")
        with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as peer:
            peer.settimeout(5); peer.connect(str(self.path))
            peer.sendall(b"CWP1" + bytes([code, len(name)]) + name + body.encode())
            data = peer.recv(65536 + 70)
        assert len(data) >= 6 and data[:4] == b"CWP1", data
        n = data[5]
        return data[4], data[6:6 + n].decode(), data[6 + n:].decode()

    def cleanup(self, command):
        self.process.stdin.write("cleanup\t" + command + "\n"); self.process.stdin.flush()
        line = self.wait(lambda value: value.startswith("cleanup "))
        return json.loads(line[8:]) if command == "status" else line[8:]

    def task(self, after=""):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            result = self.rpc(1, after)
            if result[0] == 65: return result[1:]
            assert result[0] == 64, result
            time.sleep(.01)
        raise AssertionError("no worker task")

    def kill(self):
        self.process.kill(); self.process.wait(timeout=5); self.reader.join(timeout=1)
        self.process.stdin.close(); self.process.stdout.close(); self.process.stderr.close()
        self.process = None

    def close(self):
        if self.process:
            if self.process.poll() is None:
                self.process.stdin.write("quit\n"); self.process.stdin.flush()
            self.process.wait(timeout=5); self.reader.join(timeout=1)
            stderr = self.process.stderr.read()
            assert self.process.returncode == 0, stderr
            self.process.stdin.close(); self.process.stdout.close(); self.process.stderr.close()
        self.listener.close()


# A self-contained authored policy exercises the same native intake, worker
# origins, timer envelopes and commit grants as the installed HE client.
# The divergent branch is intentional; its peers still have to complete.
PRIME_POLICY = '''
(= (activity:spin $n) (activity:spin (+ $n 1)))
(= (activity:count $n $plan)
   (if (== $n 0) $plan (activity:count (- $n 1) $plan)))
(= (activity:receive $worker)
   (match &self (host:record 0 $key
      (activity:input 1 $id $owner $generation $kind $body $due $period))
    (if (== $body "spin") (activity:spin 0)
     (if (== $kind work)
      (host:transition (activity:waiting 1 $id $owner $generation) ()
       ((host:send 0 (worker:request 1 $worker $body)
          (activity:return 1 $id $owner $generation))))
      (if (== $kind timer)
       (host:transition (activity:timing 1 $id $owner $generation) ()
        ((host:send 1 (timer:at 1 $due $period FireOnce 0 $body)
           (activity:tick 1 $id $owner $generation))))
       (activity:count 12
        (host:transition (activity:done 1 $id $owner $generation)
         ((host:put 0 $id (activity:noticed 1 $owner $generation $body))) ())))))))
(= (activity:reply $worker $result-key $task-key $intent-key)
   (match &self (host:record 0 $key (host:worker-result 1 $worker $task $revision $body))
    (match &self (host:record 5 $intent-key
       (host:intent 1 "worker.request" "1" $request (activity:return 1 $id $owner $generation)))
     (host:transition (activity:done 1 $id $owner $generation)
      ((host:put 0 $result-key (activity:result 1 $owner $generation current $body))) ()))))
(= (activity:timer-reply $result-key $intent-key $effect-id)
   (match &self (host:record 0 $key
      (host:timer-event 1 $effect-id $sequence $kind $due $now $represented $body
       (activity:tick 1 $id $owner $generation)))
    (host:transition (activity:timing 1 $id $owner $generation)
     ((host:put 0 $result-key
       (activity:timer-result 1 $owner $generation $sequence $kind $due $now $represented current $body))) ())))
'''


@unittest.skipUnless(os.environ.get("CETTA_ACTIVITY_SERVICE"), "set CETTA_ACTIVITY_SERVICE")
class PrimeActivityTests(unittest.TestCase):
    def service(self, quantum=1, work_limit=100000, capacity=4,
                cleanup_quantum=None, allow_atomic_cleanup=False):
        temporary = tempfile.TemporaryDirectory(); self.addCleanup(temporary.cleanup)
        policy = Path(temporary.name) / "policy.metta"; policy.write_text(PRIME_POLICY)
        service = Service(temporary.name, capacity, policy, quantum, work_limit,
                          cleanup_quantum, allow_atomic_cleanup)
        self.addCleanup(service.close)
        return service

    def test_retained_prime_planning_services_worker_timer_and_late_input(self):
        s = self.service()
        s.record("work", "worker", body="external observation")
        task, body = s.task(); self.assertEqual(body, "external observation")
        s.record("notice", "continuing", body="spin")
        s.record("timer", "clock", due=int(time.time() * 1000) + 20, body="tick")
        s.record("notice", "late", body="new input")
        self.assertEqual(s.rpc(2, task, "worker answer")[0], 66)
        results = {}
        for identity in ("worker", "late", "clock/0"):
            prefix = "result " + identity + " "
            results[identity] = next((l for l in s.lines if l.startswith(prefix)), None) or s.wait(
                lambda l: l.startswith(prefix), 15)
        self.assertIn('current "worker answer"', results["worker"])
        self.assertIn('"new input"', results["late"])
        self.assertIn(" fired ", results["clock/0"])
        self.assertFalse(any(l.startswith("result continuing ") for l in s.lines))

    def test_native_quantum_changes_turns_without_repeating_work(self):
        paid = []
        for quantum in (1, 7):
            s = self.service(quantum=quantum)
            s.record("notice", "same", body="same source")
            self.assertIn('"same source"', s.wait(lambda l: l.startswith("result same "), 15))
            s.close(); s.process = None
            stats = json.loads(next(l[6:] for l in s.lines if l.startswith("stats ")))
            paid.append(stats["plan_work"])
            self.assertGreater(stats["plan_polls"], 1)
            self.assertGreater(stats["plan_setup"], 0)
            self.assertGreater(stats["plan_setup_bytes"], 0)
            self.assertGreater(stats["plan_preparation"], 0)
            self.assertGreater(stats["plan_transport"], 0)
            self.assertGreater(stats["plan_admission"], 0)
            self.assertGreater(stats["plan_producer"], 0)
            self.assertGreater(stats["plan_consumer"], 0)
            self.assertLessEqual(stats["plan_final"], stats["plan_producer"])
            self.assertEqual(stats["plan_rotations"], 0)
            callbacks = sum(stats[name] for name in (
                "plan_admission", "plan_producer", "plan_consumer",
                "plan_final", "plan_rotations", "plan_retirements"))
            self.assertEqual(stats["plan_receipt_units"], callbacks +
                stats["plan_setup"] +
                stats["plan_work"] + stats["plan_preparation"] +
                stats["plan_transport"])
            self.assertEqual(stats["slot_units"], sum(stats[name] for name in (
                "slot_admission", "slot_preparation", "slot_execution",
                "slot_delivery", "slot_teardown", "slot_arrivals",
                "slot_bookkeeping")))
            self.assertGreater(stats["max_slot_units"], 0)
            self.assertLessEqual(stats["max_slot_units"], stats["slot_units"])
            self.assertGreaterEqual(stats["slot_preparation"], stats["plan_setup"])
            self.assertGreaterEqual(stats["slot_execution"], stats["plan_work"])
            self.assertGreaterEqual(stats["plan_receipt_events"], callbacks)
            print("native activity quantum", quantum, "stats", json.dumps(stats), flush=True)
        self.assertEqual(paid[0], paid[1]); self.assertGreater(paid[0], 0)

    def test_cleanup_holds_capacity_while_peer_progresses_and_regrant_releases_once(self):
        s = self.service(capacity=2, cleanup_quantum=1)
        s.record("notice", "first", body="first")
        s.wait(lambda line: line.startswith("result first "), 15)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            status = s.cleanup("status")
            if status["atomic_waits"]: break
        self.assertGreater(status["atomic_waits"], 0)
        self.assertEqual(status["completed"], 0)
        s.record("notice", "peer", body="peer")
        s.wait(lambda line: line.startswith("result peer "), 15)
        s.record("notice", "backpressure", body="retained")
        status = s.cleanup("status")
        self.assertEqual(status["ready"], 2)
        self.assertEqual(status["completed"], 0)
        self.assertFalse(any(line.startswith("result backpressure ") for line in s.lines))
        self.assertEqual(len(records(s.directory / "journal.cdr", "host.commits")), 2)
        self.assertEqual(s.cleanup("allow"), "allowed")
        s.wait(lambda line: line.startswith("result backpressure "), 15)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            status = s.cleanup("status")
            if status["ready"] == 0: break
        self.assertEqual(status["ready"], 0)
        self.assertEqual(status["completed"], 3)
        self.assertEqual(len(records(s.directory / "journal.cdr", "host.commits")), 3)
        s.close(); s.process = None
        stats = json.loads(next(line[6:] for line in s.lines if line.startswith("stats ")))
        callbacks = sum(stats[name] for name in (
            "plan_admission", "plan_producer", "plan_consumer", "plan_final",
            "plan_rotations", "plan_retirements", "plan_cleanup_polls"))
        self.assertEqual(stats["plan_receipt_units"], callbacks + stats["plan_setup"] +
                         stats["plan_work"] + stats["plan_preparation"] +
                         stats["plan_transport"] + stats["cleanup_units"])
        self.assertGreater(stats["cleanup_units"], 0)
        self.assertGreaterEqual(stats["slot_teardown"], stats["cleanup_units"])
        self.assertEqual(stats["slot_units"], sum(stats[name] for name in (
            "slot_admission", "slot_preparation", "slot_execution", "slot_delivery",
            "slot_teardown", "slot_arrivals", "slot_bookkeeping")))

    def test_zero_purse_parks_without_refill_and_keeps_capacity_backpressure(self):
        s = self.service(work_limit=0, capacity=1)
        s.record("notice", "parked")
        s.record("notice", "unadmitted")
        self.assertEqual(s.rpc(1)[0], 64)
        s.close(); s.process = None
        stats = json.loads(next(l[6:] for l in s.lines if l.startswith("stats ")))
        self.assertEqual(stats["plan_work"], 0); self.assertEqual(stats["plan_polls"], 1)
        self.assertGreater(stats["plan_setup"], 0)
        self.assertGreater(stats["plan_setup_bytes"], 0)
        self.assertEqual(stats["plan_preparation"], 0)
        self.assertEqual(stats["plan_transport"], 0)
        self.assertGreaterEqual(stats["plan_admission"], 1)
        self.assertEqual(stats["plan_consumer"], 0)
        callbacks = sum(stats[name] for name in (
            "plan_admission", "plan_producer", "plan_consumer",
            "plan_final", "plan_rotations", "plan_retirements"))
        self.assertEqual(stats["plan_receipt_units"], callbacks + stats["plan_setup"])
        self.assertEqual(stats["slot_units"], sum(stats[name] for name in (
            "slot_admission", "slot_preparation", "slot_execution",
            "slot_delivery", "slot_teardown", "slot_arrivals",
            "slot_bookkeeping")))
        self.assertGreater(stats["max_slot_units"], 0)
        self.assertEqual(stats["ready"], 1)
        self.assertEqual(len(records(s.directory / "journal.cdr", "host.inbox")), 2)
        self.assertEqual(len(records(s.directory / "journal.cdr", "host.commits")), 0)

    def test_parked_native_plan_restart_reuses_input_then_replays_completed_result(self):
        s = self.service(work_limit=0)
        s.record("notice", "restart", body="restart")
        self.assertEqual(s.rpc(1)[0], 64)
        s.kill()
        self.assertEqual(len(records(s.directory / "journal.cdr", "host.inbox")), 1)
        self.assertEqual(len(records(s.directory / "journal.cdr", "host.commits")), 0)
        # Restart owns a fresh planning ticket; the persisted input survives
        # retirement of the exhausted in-process task and its original purse.
        s.planning[-1] = "100000"
        s.start(); s.wait(lambda l: l.startswith("result restart "), 15)
        s.kill(); s.start(); s.wait(lambda l: l.startswith("result restart "), 15)
        self.assertEqual(len(records(s.directory / "journal.cdr", "host.commits")), 1)


@unittest.skipUnless(os.environ.get("CETTA_ACTIVITY_SERVICE"), "set CETTA_ACTIVITY_SERVICE")
class ActivityTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(); self.addCleanup(temporary.cleanup)
        self.service = Service(temporary.name); self.addCleanup(self.service.close)

    def test_waiting_work_does_not_block_input_or_absolute_timer(self):
        s = self.service
        s.record("work", "slow", body="owned slow request")
        task, body = s.task(); self.assertEqual(body, "owned slow request")
        due = int(time.time() * 1000) + 50
        s.record("timer", "alphabet", body="abcdefghijklmnopqrstuvwxyz", due=due, period=50)
        s.record("notice", "incoming", body="new input")
        self.assertIn("activity:noticed", s.wait(lambda l: l.startswith("result incoming ")))
        tick = s.wait(lambda l: l.startswith("result alphabet/0 "))
        self.assertIn("abcdefghijklmnopqrstuvwxyz", tick)
        self.assertIn(" fired ", tick)
        self.assertEqual(s.rpc(2, task, "slow answer")[0], 66)
        self.assertIn('current "slow answer"', s.wait(lambda l: l.startswith("result slow ")))

    def test_completions_are_selective_and_duplicate_receipts_are_stable(self):
        s = self.service
        s.record("work", "first", body="first observation")
        first, _ = s.task()
        s.record("work", "second", body="second observation")
        second, body = s.task(first); self.assertEqual(body, "second observation")
        self.assertEqual(s.rpc(2, second, "second result")[0], 66)
        s.wait(lambda l: l.startswith("result second "))
        self.assertFalse(any(l.startswith("result first ") for l in s.lines))
        self.assertEqual(s.rpc(2, second, "second result")[0], 66)
        self.assertEqual(s.rpc(2, second, "changed result")[0], 69)
        self.assertEqual(s.rpc(3, second)[0], 66)
        self.assertEqual(s.rpc(2, first, "first result")[0], 66)
        s.wait(lambda l: l.startswith("result first "))
        self.assertEqual(sum(l.startswith("result second ") for l in s.lines), 1)

    def test_revoked_wait_retains_late_result_as_inert_data(self):
        s = self.service
        s.record("work", "request", owner="strand")
        task, _ = s.task()
        s.record("cancel", "revoke", owner="strand")
        s.wait(lambda l: l.startswith("result revoke "))
        self.assertEqual(s.rpc(2, task, '(shell "must stay data")')[0], 66)
        line = s.wait(lambda l: l.startswith("result request "))
        self.assertIn(" late ", line); self.assertIn("must stay data", line)
        s.record("notice", "fresh", owner="strand", generation=2)
        s.wait(lambda l: l.startswith("result fresh "))

    def test_periodic_cancel_retires_the_native_timer_across_restart(self):
        s = self.service
        s.record("timer", "periodic", owner="clock", due=int(time.time() * 1000) + 50, period=100)
        s.wait(lambda l: l.startswith("result periodic/0 "))
        s.record("cancel", "cancel-clock", owner="clock")
        s.wait(lambda l: l.startswith("result cancel-clock "))
        cancelled = s.wait(lambda l: l.startswith("result periodic/") and " cancelled " in l)
        self.assertIn(" late ", cancelled)
        s.kill(); s.start()
        s.record("notice", "after-clock-cancel")
        s.wait(lambda l: l.startswith("result after-clock-cancel "))
        before = len(s.lines)
        # Native deadlines remain armed after restart unless cancellation
        # actually retires their resource, rather than only revoking a wait.
        time.sleep(.35)
        self.assertFalse(any(l.startswith("result periodic/") for l in s.lines[before:]))

    def test_continuation_and_result_survive_coordinator_kill(self):
        s = self.service
        s.record("work", "restart")
        task, _ = s.task()
        s.kill(); s.start()
        self.assertEqual(s.task()[0], task)
        self.assertEqual(s.rpc(2, task, "after restart")[0], 66)
        s.wait(lambda l: l.startswith("result restart "))
        s.kill(); s.start()
        line = s.wait(lambda l: l.startswith("result restart "))
        self.assertIn("after restart", line)
        self.assertEqual(s.rpc(2, task, "after restart")[0], 66)
        self.assertEqual(s.rpc(1)[0], 64)

    def test_duplicate_input_does_not_recreate_consumed_work(self):
        s = self.service
        s.record("notice", "dedupe", body="original")
        s.wait(lambda l: l.startswith("result dedupe "))
        self.assertTrue(s.record("notice", "dedupe", body="original").endswith("ok"))
        self.assertTrue(s.record("notice", "dedupe", body="changed").endswith("conflict"))
        s.record("notice", "barrier"); s.wait(lambda l: l.startswith("result barrier "))
        self.assertEqual(sum(l.startswith("result dedupe ") for l in s.lines), 1)

    def test_duplicate_owner_is_rejected_without_another_provider_task(self):
        s = self.service
        s.record("work", "original", owner="owner")
        task, _ = s.task()
        s.record("work", "duplicate", owner="owner")
        self.assertIn("activity:rejected", s.wait(lambda l: l.startswith("result duplicate ")))
        self.assertEqual(s.rpc(1, task)[0], 64)
        s.rpc(2, task, "original result"); s.wait(lambda l: l.startswith("result original "))

    def test_capacity_one_preserves_arrivals_and_foreign_submissions(self):
        s = self.service
        s.kill(); s.capacity = 1; s.start()
        self.assertEqual(s.rpc(4, "foreign", '(host:transition forged () ())')[0], 66)
        for i in range(12):
            s.process.stdin.write(f"notice\tn{i}\tn{i}\t1\tarrival {i}\n")
        s.process.stdin.flush()
        found = set()
        while len(found) < 12:
            line = s.wait(lambda l: l.startswith("result n"))
            found.add(line.split()[1])
        self.assertEqual(found, {f"n{i}" for i in range(12)})
        self.assertFalse(any("forged" in l for l in s.lines))

    def test_slow_result_consumer_cannot_block_the_worker_receipt_channel(self):
        # Deliberately leave stdout unread after the initial intake. A large
        # result fills that pipe; it must not freeze IPC for other activities.
        with tempfile.TemporaryDirectory() as directory:
            path = str(Path(directory) / "worker.sock")
            with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as listener:
                listener.bind(path); listener.listen(16)
                process = subprocess.Popen([os.environ["CETTA_ACTIVITY_SERVICE"],
                    str(Path(directory) / "journal.cdr"), str(listener.fileno()), "activities", "4"],
                    pass_fds=[listener.fileno()], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                try:
                    self.assertEqual(process.stdout.readline(), b"ready\n")
                    process.stdin.write(b"work\tlarge\tlarge\t1\trequest\n"); process.stdin.flush()
                    self.assertEqual(process.stdout.readline(), b"intake large ok\n")
                    def rpc(code, identity="", body=""):
                        name = identity.encode()
                        with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as peer:
                            peer.settimeout(2); peer.connect(path)
                            peer.sendall(b"CWP1" + bytes([code, len(name)]) + name + body.encode())
                            data = peer.recv(65536 + 70)
                        return data[4], data[6:6 + data[5]].decode()
                    deadline = time.monotonic() + 5
                    while True:
                        code, task = rpc(1)
                        if code == 65: break
                        self.assertLess(time.monotonic(), deadline); time.sleep(.01)
                    self.assertEqual(rpc(2, task, "x" * 65536)[0], 66)
                    time.sleep(.1)
                    self.assertEqual(rpc(3, task)[0], 66)
                    process.terminate(); process.wait(timeout=2)
                    self.assertEqual(process.returncode, 0)
                finally:
                    if process.poll() is None: process.kill(); process.wait(timeout=5)
                    process.stdin.close(); process.stdout.close(); process.stderr.close()


if __name__ == "__main__":
    unittest.main(verbosity=2)
