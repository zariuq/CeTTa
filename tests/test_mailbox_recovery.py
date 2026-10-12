"""Availability checks through real native commits and disposable evaluators."""
import os
import signal
import time
import unittest
from unittest.mock import patch

from test_native_mailbox import Fixture, SERVER, WORKER
from native_mailbox import Mailbox
from space_service_client import E, Var, Uncertain


@unittest.skipUnless(SERVER and WORKER, "set CETTA_SPACE_SERVICE and CETTA_VALUE_WORKER")
class MailboxRecoveryTests(unittest.TestCase):
    def setUp(self):
        self.fixture = Fixture()
        self.addCleanup(self.fixture.close)
        self.box = Mailbox(self.fixture.socket, "recovery", WORKER)
        self.addCleanup(self.box.close)

    def test_peer_closed_before_send_is_reopened_without_replaying_a_packet(self):
        connection = self.box.client.connection
        connection.shutdown(2)
        self.box.client.hello()
        self.assertEqual(self.box.listen("inbox", "peer", "arrival"), "listening")
        self.assertIsNot(self.box.client.connection, connection)

    def test_lost_commit_reply_reconciles_identity_instead_of_resending_old_batch(self):
        original = self.box.client.call
        batches = []

        def lose_reply(operation, *args, **kwargs):
            value = original(operation, *args, **kwargs)
            if operation == "batch":
                batches.append(args)
                if len(batches) == 1:
                    raise Uncertain(E("lost-reply"))
            return value

        with patch.object(self.box.client, "call", side_effect=lose_reply):
            self.assertEqual(self.box.send("one", "inbox", "reply", "peer", "body"), "duplicate")
        self.assertEqual(len(batches), 1)
        self.assertEqual(self.box.space.match_count(E("MailboxMessage", 1, Var(0), Var(1), Var(2), Var(3), Var(4))), 1)
        self.assertEqual(self.box.pending("inbox", "peer")[-1], "one")

    def test_recovery_is_bounded_when_no_reply_can_be_observed(self):
        with patch.object(self.box.client, "call", side_effect=Uncertain(E("lost"))) as call:
            with self.assertRaises(Uncertain):
                self.box.listen("inbox", "peer", "arrival")
        self.assertEqual(call.call_count, 2)

    def test_killed_evaluator_is_replaced_before_another_pure_plan(self):
        self.box.listen("inbox", "peer", "arrival")
        old = self.box.worker.process
        old.kill(); old.wait(timeout=5)
        self.assertEqual(self.box.send("one", "inbox", "reply", "peer", "body"), "accepted")
        self.assertNotEqual(self.box.worker.process.pid, old.pid)
        self.assertIsNotNone(old.poll())

    def test_stalled_evaluator_is_reaped_and_replanned_from_current_state(self):
        self.box.listen("inbox", "peer", "arrival")
        old = self.box.worker.process
        self.box.worker.timeout = .2
        os.kill(old.pid, signal.SIGSTOP)
        started = time.monotonic()
        self.assertEqual(self.box.send("one", "inbox", "reply", "peer", "body"), "accepted")
        self.assertLess(time.monotonic() - started, 5)
        self.assertIsNotNone(old.poll())
        self.assertNotEqual(self.box.worker.process.pid, old.pid)
        self.assertEqual(self.box.pending("inbox", "peer")[-1], "one")


if __name__ == "__main__":
    unittest.main()
