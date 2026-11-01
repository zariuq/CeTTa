"""Count revision retains origins and occurrences without counting copies twice."""
import argparse
from collections import Counter
from fractions import Fraction
import json
import math
from pathlib import Path
import random
import struct
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from space_service_client import E, S, Var, FloatBits, encode
from value_worker_client import Worker
from check_native_kb_service import Server, Client, await_resources
from check_space_query import rows, bag, open_query, drain


def number(value):
    if isinstance(value, FloatBits):
        return struct.unpack("<d", struct.pack("<Q", value.bits))[0]
    assert type(value) is int, value
    return value


def view_check(view, positive, negative):
    assert view[0] == S("stv"), view
    total = positive + negative
    expected = (Fraction(positive, total) if total else Fraction(0), Fraction(total, total + 1))
    assert all(math.isclose(number(actual), float(wanted), rel_tol=1e-14, abs_tol=1e-14)
               for actual, wanted in zip(view[1:], expected)), (view, expected)


def accepted(result, positive=9, negative=6, supports=4):
    assert result[0] == S("EvidenceRevisionV1"), result
    assert result[1:4] == (S("claim1"), E("EvidenceCalibrationV1", S("natural-counts"), S("independent-origin-claim"), 1), E("evidence", positive, negative)), result
    view_check(result[4], positive, negative)
    assert sum(len(unit[3]) for unit in result[5]) == supports, result
    assert all(unit[4] == S("NoEvidenceProblemV1") for unit in result[5]), result
    # Lists have explicit canonical order; don't erase it with a bag comparator.
    assert list(map(encode, result[5])) == sorted(map(encode, result[5]))
    for unit in result[5]:
        assert list(map(encode, unit[3])) == sorted(map(encode, unit[3]))
        assert all(len(row[1][7]) == 2 and len(row[1][8]) == len(row[2]) == 1 for row in unit[3])


def refused(result, reason):
    assert result[0] == S("EvidenceRevisionRefusedV1"), result
    assert any(unit[4] == S(reason) for unit in result[2]), (reason, result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worker", type=Path)
    parser.add_argument("--rounds", type=int, default=100)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    assert args.rounds > 0
    fixture = ROOT / "tests/support/space_evidence_fixture.metta"
    binary = args.worker.resolve()
    with tempfile.TemporaryDirectory(prefix="space-evidence-") as directory:
        root = Path(directory); path = root / "store"; path.mkdir(mode=0o700)
        worker = Worker(binary, fixture, "evidence-test:run", language="petta")
        server = Server(binary, root, module=fixture, entry="evidence-test:configuration")
        client = None
        try:
            rng = random.Random(61031)
            for i in range(64):
                a, b, c, d = [rng.randrange(25) for _ in range(4)]
                result = worker.call(E("Kernel", E("evidence", a, b), E("evidence", c, d)))
                assert result[0:2] == (S("EvidenceKernelV1"), E("evidence", a + c, b + d)), result
                view_check(result[2], a + c, b + d)
            zero = worker.call(E("Kernel", E("evidence", 0, 0), E("evidence", 0, 0)))
            view_check(zero[2], 0, 0)
            assert worker.call(E("Install", str(path)))[0] == S("KnowledgeCommittedV1")
            assert worker.call(E("Install", str(path)))[0] == S("KnowledgeReplayedV1")
            plans = worker.call(S("Plans")); supports, units, revision = plans[1:]
            def local(plan=revision, count=2, fuel=4):
                return rows(worker.call(E("Query", str(path), plan, count, fuel)))
            original_supports = local(supports)
            assert len(original_supports) == 4
            baseline = local()[0]; accepted(baseline)
            assert {unit[1]: len(unit[3]) for unit in baseline[5]} == {S("origin1"): 3, S("origin2"): 1}
            assert {row[1][3] for row in original_supports} == {S(f"evidence{i}") for i in range(1, 5)}
            calibration = {row[1][1]: row[2][0] for row in original_supports}
            evidence_refs = {row[1][3]: row[1][7][1] for row in original_supports}
            # A naive sum over joined copies yields another strength/confidence.
            naive = (sum(row[1][5] for row in original_supports), sum(row[1][6] for row in original_supports))
            assert naive == (25, 10) and naive != (9, 6)
            assert float(Fraction(naive[0], sum(naive))) != number(baseline[4][1])
            worker.close(); worker = Worker(binary, fixture, "evidence-test:run", language="petta")
            assert local()[0] == baseline
            server.start(); client = Client(server.path)
            def remote(plan=revision):
                view, _ = client.snapshot()
                try:
                    return drain(client, open_query(client, view, plan), 1, 8, replay=True)[0]
                finally:
                    client.drop(view)
            for plan in plans[1:]:
                assert bag(remote(plan)) == bag(local(plan)), plan
            assert remote()[0] == baseline
            def commit(request, changes):
                view, _ = client.snapshot()
                try:
                    answer = client.route("Commit", E("CommitV1", S("curriculum"), request, tuple(changes)), view)
                    assert answer[0] == S("KnowledgeCommittedV1"), answer
                    return answer[1][1]
                finally:
                    client.drop(view)
            duplicate = commit("duplicate-calibration", [E("SpaceInsertV1", 0, E("calibrated-evidence", S("origin1"), S("claim1"), 8, 2))])[0]
            accepted(remote()[0], supports=7)
            commit("drop-duplicate-calibration", [E("SpaceDeleteV1", duplicate)])
            copied = commit("source-copy", [
                E("SpaceInsertV1", 0, E("source", S("source4"), S("origin1"), "Another station copy")),
                E("SpaceInsertV1", 1, E("judgement", S("judgement5"), S("claim1"), S("supports"), E("Assessment", FloatBits.of(.8), FloatBits.of(.6)))),
                E("SpaceInsertV1", 2, E("evidence", S("evidence5"), S("judgement5"), S("source4"))),
                E("SpaceInsertV1", 3, E("derived-from", S("lineage2"), S("source4"), S("source1")))])
            accepted(remote()[0], supports=5)
            commit("retract-copy", [E("SpaceDeleteV1", copied[2])])
            accepted(remote()[0])
            conflict = commit("conflicting-calibration", [E("SpaceInsertV1", 0, E("calibrated-evidence", S("origin1"), S("claim1"), 9, 2))])[0]
            refused(remote()[0], "ConflictingEvidenceCalibrationV1")
            commit("remove-conflict", [E("SpaceDeleteV1", conflict)])
            for i, value in enumerate([-1, FloatBits.of(2.0), True, E("danger"), FloatBits.of(float("nan"))]):
                commit(f"bad-count-{i}", [E("SpaceUpdateV1", calibration[S("origin1")], E("calibrated-evidence", S("origin1"), S("claim1"), value, 2))])
                refused(remote()[0], "InvalidEvidenceCountsV1")
            assert client.route("EffectCount") == worker.call(E("EffectCount")) == 0
            commit("restore-counts", [E("SpaceUpdateV1", calibration[S("origin1")], E("calibrated-evidence", S("origin1"), S("claim1"), 8, 2))])
            # A stored open calibration is outside this ground-data projection;
            # it cannot manufacture counts for an otherwise missing origin.
            commit("drop-counts", [E("SpaceDeleteV1", ref) for ref in calibration.values()])
            open_ref = commit("open-calibration", [E("SpaceInsertV1", 0, E("calibrated-evidence", Var(0), S("claim1"), 8, 2))])[0]
            missing = remote()[0]; refused(missing, "MissingEvidenceCalibrationV1")
            assert all(unit[2] == S("NoEvidenceCountsV1") for unit in missing[2])
            commit("drop-open", [E("SpaceDeleteV1", open_ref)])
            commit("restore-both", [E("SpaceInsertV1", 0, E("calibrated-evidence", S("origin1"), S("claim1"), 8, 2)),
                                    E("SpaceInsertV1", 1, E("calibrated-evidence", S("origin2"), S("claim1"), 1, 4))])
            accepted(remote()[0])
            # The old query belongs to its captured revision despite retraction
            # and compact/collect before it has read the evidence rows.
            view, _ = client.snapshot(); captured = open_query(client, view, revision); client.drop(view)
            commit("retract-origin1", [E("SpaceDeleteV1", evidence_refs[S(name)]) for name in ["evidence1", "evidence2", "evidence4"]])
            assert client.route("Compact")[0] == S("StoreCompacted")
            old = drain(client, captured, 1, 8)[0][0]; accepted(old)
            revised = remote()[0]; accepted(revised, 1, 4, 1)
            server.stop(crash=True); client.close(); server.start(); client = Client(server.path)
            assert remote()[0] == revised
            samples = []
            for i in range(args.rounds + 24):
                view, _ = client.snapshot(); token = open_query(client, view, revision); client.drop(view)
                pending = client.call(E("UseValueV1", token, S("RelPage"), E("RelPageV1", 1, 32)))
                assert pending[0] == S("ServiceResourceV1"), pending
                token = pending[1]
                assert client.call(E("UseValueV1", token, S("RelCancel"), ())) == S("RelCancelledV1")
                if i in [23, args.rounds + 23]:
                    await_resources(client, 0); samples.append(server.sample(client, i - 23))
            assert samples[0]["descriptors"] == samples[1]["descriptors"] and samples[0]["stats"] == samples[1]["stats"], samples
            commit("retract-origin2", [E("SpaceDeleteV1", evidence_refs[S("evidence3")])])
            accepted(remote()[0], 0, 0, 0)
            report = {"kernel_oracle_cases": 65, "initial_supports": 4, "origin_support_counts": {"origin1": 3, "origin2": 1},
                      "deduplicated_counts": [9, 6], "naive_counts": list(naive), "duplicate_calibration_supports": 7,
                      "copied_source_supports": 5, "invalid_count_controls": 5, "open_data_cannot_manufacture_counts": True,
                      "conflicting_and_missing_calibrations_refused": True, "captured_retraction_compaction_restart": True,
                      "empty_evidence_confidence": 0, "effects": 0, "rounds": args.rounds, "samples": samples}
            if args.output:
                args.output.parent.mkdir(parents=True, exist_ok=True); args.output.write_text(json.dumps(report, indent=2) + "\n")
            print("PASS:qualified count revision over captured Spaces;origins,provenance,refusals,effects,retraction/restart and cleanup")
        finally:
            if client: client.close()
            server.stop(); worker.close()


if __name__ == "__main__": main()
