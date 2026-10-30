"""Application-neutral native facts, observations and durable receipts.

The executable reference is a typed Python value map in these fixtures, not a
SQL schema. Runtime semantics execute in the SQLite-free MeTTa value worker.
"""
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
CETTA_ROOT = Path(os.environ.get("CETTA_CHANNEL_ROOT", ROOT))
WORKER = os.environ.get("CETTA_VALUE_WORKER")
if WORKER:
    sys.path.insert(0, str(CETTA_ROOT / "tools"))
    from value_worker_client import Worker
    from space_service_client import E, S, BigInt, FloatBits, Var, encode


@unittest.skipUnless(WORKER, "set CETTA_VALUE_WORKER to the native store worker")
class KnowledgeStoreTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="native-knowledge-")
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.path = self.directory / "store"
        self.path.mkdir(mode=0o700)
        self.worker = self.make_worker()
        self.addCleanup(lambda: self.worker.close())
        answer = self.worker.call(E("Create", str(self.path)))
        self.assertEqual(answer[0], S("KnowledgeReadyV1"), answer)
        self.epoch = answer[1]
        self.sequence = 0

    def make_worker(self, **kwargs):
        environment = {"HOME": str(self.directory), "PATH": os.environ.get("PATH", "/usr/bin:/bin")}
        environment.update(kwargs)
        with patch.dict(os.environ, environment, clear=True):
            return Worker(WORKER, ROOT / "tests/support/kb_knowledge_probe.metta",
                          "knowledge-probe:run", language="petta", import_mode="relative")

    def same(self, actual, expected):
        self.assertEqual(encode(actual), encode(expected))

    def at(self, operation):
        return self.worker.call(E("At", str(self.path), operation))

    def application(self, changes, ids=(), *, receipt=None, command=None, answer=None):
        self.sequence += 1
        if receipt is None:
            receipt = E("Receipt", self.sequence)
        if command is None:
            command = E("Edit", tuple(changes))
        if answer is None:
            answer = E("Accepted", self.sequence)
        return E("KnowledgeApplyV1", tuple(ids), receipt, command, answer, tuple(changes), 1048576)

    def apply(self, changes, ids=(), **kwargs):
        return self.at(self.application(changes, ids, **kwargs))

    def values(self, ids):
        answer = self.at(E("KnowledgeReadV1", tuple(ids), 1048576))
        self.assertEqual(answer[0], S("KnowledgeObservedV1"), answer)
        return tuple(point[2] for point in answer[1])

    def revision(self):
        answer = self.at(S("KnowledgeProbeRevisionV1"))
        self.assertEqual(answer[:2], (S("KnowledgeRevisionV1"), self.epoch))
        return answer[2]

    def audit(self):
        answer = self.at(S("KnowledgeProbeAuditV1"))
        self.assertEqual(answer[0], S("KnowledgeAuditV1"), answer)
        return {encode(key): encode(value) for key, value in answer[1]}

    def test_value_kinds_and_identity_kinds_do_not_collapse(self):
        identities = [1, True, S("True"), "1", FloatBits(0x3ff0000000000000),
                      FloatBits(0x8000000000000000), FloatBits(0), BigInt(str(1 << 100)),
                      "a\0b", "a\0c", b"\xff", "é", "e\u0301", E("Occurrence", 1)]
        values = [E("Value", value, False, S("Unknown")) for value in identities]
        changes = [E("KnowledgePutV1", identity, value) for identity, value in zip(identities, values)]
        result = self.apply(changes)
        self.assertEqual(result[0], S("KnowledgeCommittedV1"), result)
        self.same(self.values(identities), tuple(E("KnowledgePresentV1", value) for value in values))
        self.assertEqual(len(self.audit()), len(identities) + 1)

    def test_equal_payloads_are_separate_identified_facts(self):
        identities = [E("Occurrence", "first"), E("Occurrence", "second")]
        payload = E("Observation", "same", S("Unknown"))
        self.apply([E("KnowledgePutV1", identity, payload) for identity in identities])
        self.same(self.values(identities), (E("KnowledgePresentV1", payload),) * 2)
        self.apply([E("KnowledgeRemoveV1", identities[0])])
        self.same(self.values(identities), (S("KnowledgeAbsentV1"), E("KnowledgePresentV1", payload)))

    def test_fact_identities_are_stable_while_payloads_can_contain_syntax_variables(self):
        for identity in (Var(7), E("Occurrence", Var(11))):
            self.same(self.apply([E("KnowledgePutV1", identity, "value")]),
                      E("KnowledgeRefusedV1", S("nonground-identity")))
        payload = E("Rule", Var(11), Var(11), E("Body", Var(12)))
        self.assertEqual(self.apply([E("KnowledgePutV1", "rule", payload)])[0], S("KnowledgeCommittedV1"))
        self.same(self.values(["rule"]), (E("KnowledgePresentV1", payload),))

    def test_receipt_replay_returns_the_original_answer_and_does_not_reapply(self):
        command = E("Change", "x", "original")
        first = self.application([E("KnowledgePutV1", "x", "original")], receipt="same", command=command,
                                 answer="original answer")
        self.assertEqual(self.at(first)[0], S("KnowledgeCommittedV1"))
        self.apply([E("KnowledgePutV1", "x", "later")])
        revision, before = self.revision(), self.audit()
        replay = self.application([E("KnowledgePutV1", "x", "wrong")], receipt="same", command=command,
                                  answer="wrong answer")
        self.same(self.at(replay), E("KnowledgeReplayedV1", "original answer"))
        self.assertEqual(self.revision(), revision)
        self.assertEqual(self.audit(), before)
        self.same(self.values(["x"]), (E("KnowledgePresentV1", "later"),))

    def test_receipt_identity_cannot_name_a_different_command(self):
        self.apply([E("KnowledgePutV1", "x", 1)], receipt="same", command=E("Command", 1))
        before, revision = self.audit(), self.revision()
        self.same(self.apply([E("KnowledgePutV1", "x", 2)], receipt="same", command=E("Command", True)),
                  E("KnowledgeRefusedV1", S("receipt-collision")))
        self.assertEqual(self.audit(), before)
        self.assertEqual(self.revision(), revision)

    def test_ordered_changes_and_missing_deletion_have_declared_semantics(self):
        self.apply([E("KnowledgePutV1", "x", 1), E("KnowledgeRemoveV1", "x"),
                    E("KnowledgePutV1", "x", 2), E("KnowledgeRemoveV1", "missing")])
        self.same(self.values(["x", "missing"]), (E("KnowledgePresentV1", 2), S("KnowledgeAbsentV1")))

    def test_invalid_tail_cannot_publish_a_valid_prefix_or_receipt(self):
        for malformed in [S("bad"), (), E("put", "x", 2), E("KnowledgePutV1", "short")]:
            with self.subTest(malformed=malformed):
                before, revision = self.audit(), self.revision()
                result = self.apply([E("KnowledgePutV1", "x", 1), malformed])
                self.same(result, E("KnowledgeRefusedV1", S("mutation-format")))
                self.assertEqual(self.audit(), before)
                self.assertEqual(self.revision(), revision)

    def test_point_dependencies_include_absence_and_independent_facts_commute(self):
        first = self.application([E("KnowledgePutV1", "x", 1)], ["x"])
        same = self.application([E("KnowledgePutV1", "x", 2)], ["x"])
        answer = self.at(E("KnowledgeRaceV1", first, same))
        self.assertEqual(answer[1][0], S("KnowledgeCommittedV1"), answer)
        self.same(answer[2], E("StoreRefused", S("conflict")))
        left = self.application([E("KnowledgePutV1", "a", 1)], ["a"])
        right = self.application([E("KnowledgePutV1", "b", 2)], ["b"])
        answer = self.at(E("KnowledgeRaceV1", left, right))
        self.assertEqual([value[0] for value in answer[1:]], [S("KnowledgeCommittedV1")] * 2, answer)

    def test_same_receipt_cannot_publish_twice_from_concurrent_views(self):
        first = self.application([E("KnowledgePutV1", "a", 1)], receipt="same", command="command", answer="one")
        second = self.application([E("KnowledgePutV1", "b", 2)], receipt="same", command="command", answer="two")
        answer = self.at(E("KnowledgeRaceV1", first, second))
        self.assertEqual(answer[1][0], S("KnowledgeCommittedV1"), answer)
        self.same(answer[2], E("StoreRefused", S("conflict")))
        self.same(self.values(["a", "b"]), (E("KnowledgePresentV1", 1), S("KnowledgeAbsentV1")))

    def test_declared_decision_read_is_required_separately_from_its_write(self):
        self.apply([E("KnowledgePutV1", "guard", "allowed")])
        first = self.application([E("KnowledgePutV1", "guard", "refused")])
        second = self.application([E("KnowledgePutV1", "effect", "planned")], ["guard"])
        answer = self.at(E("KnowledgeRaceV1", first, second))
        self.same(answer[2], E("StoreRefused", S("conflict")))
        # Separating negative control: omitting the influencing read really
        # allows the incorrect decision. The store cannot infer that read.
        first = self.application([E("KnowledgePutV1", "guard", "changed-again")])
        weak = self.application([E("KnowledgePutV1", "effect", "planned")])
        answer = self.at(E("KnowledgeRaceV1", first, weak))
        self.assertEqual(answer[2][0], S("KnowledgeCommittedV1"), answer)

    def test_revocation_invalidates_a_prepared_write_without_conflicting_with_other_grants(self):
        # The trusted caller supplies the principal and checks the grant.
        # This tests the storage obligation: validation must cover that
        # authority fact as well as application data, on the same view.
        alice = E("StorageGrantKeyV1", "alice", "archive", S("Write"))
        bob = E("StorageGrantKeyV1", "bob", "archive", S("Write"))
        active = E("StorageGrantV1", "grant", 1, S("Active"))
        revoked = E("StorageGrantV1", "grant", 2, S("Revoked"))
        self.apply([E("KnowledgePutV1", alice, active), E("KnowledgePutV1", bob, active)])
        other = self.application([E("KnowledgePutV1", bob, revoked)])
        prepared = self.application([E("KnowledgePutV1", "first", "value")], [alice, "first"])
        answer = self.at(E("KnowledgeRaceV1", other, prepared))
        self.assertEqual([value[0] for value in answer[1:]], [S("KnowledgeCommittedV1")] * 2, answer)

        revoke = self.application([E("KnowledgePutV1", alice, revoked)])
        stale = self.application([E("KnowledgePutV1", "second", "value")], [alice, "second"],
                                 receipt="stale-authority", command="prepared", answer="accepted")
        answer = self.at(E("KnowledgeRaceV1", revoke, stale))
        self.assertEqual(answer[1][0], S("KnowledgeCommittedV1"), answer)
        self.same(answer[2], E("StoreRefused", S("conflict")))
        self.same(self.values(["second"]), (S("KnowledgeAbsentV1"),))
        resolved = self.at(E("KnowledgeResolveV1", "stale-authority", "prepared", 1048576))
        self.assertEqual(resolved[0], S("KnowledgeUnrecordedV1"), resolved)

    def test_captured_view_is_repeatable_across_publication(self):
        first = self.application([E("KnowledgePutV1", "x", "new")])
        answer = self.at(E("KnowledgeHeldReadV1", first, ("x",), 1048576))
        self.same(answer[1], answer[3])
        self.same(answer[1][1][0][2], S("KnowledgeAbsentV1"))
        self.same(self.values(["x"]), (E("KnowledgePresentV1", "new"),))

    def test_recovery_receipt_and_fact_survive_a_worker_restart_and_compaction(self):
        command = E("Store", "x", "payload")
        self.apply([E("KnowledgePutV1", "x", "payload")], receipt="receipt", command=command, answer="result")
        self.assertEqual(self.worker.close(), 0)
        self.worker = self.make_worker()
        result = self.at(E("KnowledgeResolveV1", "receipt", command, 1048576))
        self.same(result, E("KnowledgeResolvedV1", "result", self.epoch, 1))
        before = self.audit()
        self.assertEqual(self.at(S("KnowledgeProbeCompactV1"))[0], S("KnowledgeMaintainedV1"))
        self.assertEqual(self.audit(), before)
        self.same(self.at(E("KnowledgeResolveV1", "absent", command, 1048576)),
                  E("KnowledgeUnrecordedV1", self.epoch, 1))

    def test_expressions_are_inert_values_and_materialization_failure_is_not_absence(self):
        sentinel = self.directory / "must-not-exist"
        payloads = [E("+", 2, 3), E("system", "touch " + str(sentinel)),
                    E("Error", "source", "inert"), E("KnowledgeRemoveV1", "other")]
        self.apply([E("KnowledgePutV1", i, value) for i, value in enumerate(payloads)])
        self.same(self.values(range(len(payloads))), tuple(E("KnowledgePresentV1", value) for value in payloads))
        self.assertFalse(sentinel.exists())
        result = self.at(E("KnowledgeReadV1", (0,), 1))
        self.assertEqual(result[0], S("StoreTooLarge"), result)


if __name__ == "__main__":
    unittest.main()
