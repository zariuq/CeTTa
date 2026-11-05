"""Ordinary MeTTa matching over atomically maintained durable candidate indexes."""
import argparse
from collections import Counter
import json
from pathlib import Path
import random
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from space_service_client import E, S, Var, encode
from value_worker_client import Worker
from check_native_kb_service import Server, Client, await_resources


def bag(values):
    return Counter(encode(value) for value in values)


def check_index_operations_equivalence(worker, root):
    eq_path = root / "equiv-store"
    eq_path.mkdir(mode=0o700)
    assert worker.call(E("CreatePlain", str(eq_path)))[0] == S("KnowledgeCommittedV1")
    def eq_call(cmd):
        return worker.call(E("At", str(eq_path), cmd))
    ledger_specs = (
        E("SpaceIndexV1", 3, 0), E("SpaceIndexV1", 4, 0), E("SpaceIndexV1", 5, 0), E("SpaceIndexV1", 6, 0),
        E("SpaceIndexV1", 8, 0), E("SpaceIndexV1", 9, 0), E("SpaceIndexV1", 10, 0), E("SpaceIndexV1", 11, 0),
        E("SpaceIndexV1", 3, 1), E("SpaceIndexV1", 11, 1), E("SpaceIndexV1", 11, 2),
        E("SpaceIndexV1", 11, 4), E("SpaceIndexV1", 11, 8),
        E("SpaceIndexV1", 10, 1), E("SpaceIndexV1", 10, 5), E("SpaceIndexV1", 4, 1),
        E("SpaceIndexV1", 5, 1), E("SpaceIndexV1", 9, 1), E("SpaceIndexV1", 9, 2),
        E("SpaceIndexV1", 9, 6), E("SpaceIndexV1", 9, 7), E("SpaceIndexV1", 6, 1), E("SpaceIndexV1", 8, 1),
    )
    range_specs = ledger_specs + (
        E("SpaceRangeIndexV1", 3, 2), E("SpaceRangeIndexV1", 9, 5), E("SpaceRangeIndexV1", 11, 6),
    )
    ledger_desc = eq_call(E("Descriptors", S("inputs"), ledger_specs))
    assert ledger_desc[0] == S("SpaceIndexesV1") and len(ledger_desc[1]) == 23, ledger_desc
    ledger_idxs = ledger_desc[1]
    mixed_desc = eq_call(E("Descriptors", S("inputs"), range_specs))
    assert mixed_desc[0] == S("SpaceIndexesV1") and len(mixed_desc[1]) == 26, mixed_desc
    mixed_idxs = mixed_desc[1]
    rng = random.Random(42)
    leaves = [
        S("ledger:action"), S("input-notice"), S("request"), S("processed-input"),
        S("unobserved"), S("observed"), S("True"), S("False"), S("+"),
        "ticket-1", "a\0b", "", "hello world",
        -(1 << 63), -42, -1, 0, 1, 42, (1 << 63) - 1,
        0.0, -0.0, 3.14159, -2.5, True, False,
        Var(0), Var(1), E("nested", S("a"), 7), E("pair", Var(0), "str"),
    ]
    def rand_atom():
        if rng.randint(0, 6) == 0:
            return rng.choice(leaves)
        arity = rng.choice([1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12])
        return tuple(rng.choice(leaves) for _ in range(arity))
    checked = 0
    for i in range(64):
        a1 = rand_atom()
        a2 = rand_atom()
        key = f"occ/{i}"
        b1 = eq_call(E("PutRaw", key, a1))
        b2 = eq_call(E("Encode", a2))
        for idxs in ((), ledger_idxs, mixed_idxs):
            for delta, op in ((1, E("put", key, b1)), (0, E("put", key, b2)), (-1, E("remove", key))):
                res = eq_call(E("CompareIndexOps", idxs, key, delta, op))
                assert res[0] == S("IndexOpsComparison") and encode(res[1]) == encode(res[2]), (i, delta, res)
                checked += 1
    b_valid = eq_call(E("Encode", E("edge", S("alice"), S("bob"))))
    error_cases = [
        (ledger_idxs, "occ/0", 1, E("bad", "occ/0"), E("SpaceRefusedV1", S("index-operation"))),
        (ledger_idxs, "occ/0", 1, E("put", "occ/0"), E("SpaceRefusedV1", S("index-operation"))),
        (ledger_idxs, "occ/0", 1, E("remove", "occ/0", "extra"), E("SpaceRefusedV1", S("index-operation"))),
        (ledger_idxs, "occ/0", 1, E("put", "occ/0", 123), E("StoreRefused", S("invalid-decode"))),
        (ledger_idxs, "occ/0", 1, E("put", "occ/0", "not-cav1"), E("StoreCodecRefused", "corrupt")),
        (ledger_idxs, "occ/missing", 0, E("remove", "occ/missing"), S("StoreAbsent")),
        (ledger_idxs, 123, 1, E("put", "occ/0", b_valid), E("SpaceRefusedV1", S("index-operation"))),
        (ledger_idxs, 123, -1, E("remove", "occ/0"), E("StoreRefused", S("invalid-get"))),
        (S("not-a-list"), "occ/0", 1, E("put", "occ/0", b_valid), E("SpaceRefusedV1", S("index-list"))),
        ((E("BadIndexV1", 3, 1, "idx/"),), "occ/0", 1, E("put", "occ/0", b_valid), E("SpaceRefusedV1", S("index-format"))),
        ((E("StoreSpaceIndexV1", 0, 0, "idx/"),), "occ/0", 1, E("put", "occ/0", b_valid), E("SpaceRefusedV1", S("index-format"))),
        ((E("StoreSpaceIndexV1", 3, -1, "idx/"),), "occ/0", 1, E("put", "occ/0", b_valid), E("SpaceRefusedV1", S("index-format"))),
        ((E("StoreSpaceIndexV1", 3, 3, "idx/"),), "occ/0", 1, E("put", "occ/0", b_valid), E("SpaceRefusedV1", S("index-format"))),
        ((E("StoreSpaceIndexV1", 2.5, 0, "idx/"),), "occ/0", 1, E("put", "occ/0", b_valid), E("SpaceRefusedV1", S("index-format"))),
        ((E("StoreSpaceIndexV1", 3, 1, 123),), "occ/0", 1, E("put", "occ/0", b_valid), E("SpaceRefusedV1", S("index-format"))),
        ((E("StoreSpaceIndexV1", 3, 1, ""),), "occ/0", 1, E("put", "occ/0", b_valid), E("SpaceRefusedV1", S("index-format"))),
        ((E("StoreSpaceRangeIndexV1", 3, 5, "idx/"),), "occ/0", 1, E("put", "occ/0", b_valid), E("SpaceRefusedV1", S("index-format"))),
    ]
    for idxs, k, delta, op, expected_err in error_cases:
        res = eq_call(E("CompareIndexOps", idxs, k, delta, op))
        assert res[0] == S("IndexOpsComparison") and encode(res[1]) == encode(res[2]) == encode(expected_err), res
        checked += 1
    res_closed = eq_call(E("CompareIndexOpsClosed", ledger_idxs, "occ/0", -1, E("remove", "occ/0")))
    assert res_closed[0] == S("IndexOpsComparison") and encode(res_closed[1]) == encode(res_closed[2]) == encode(E("StoreRefused", S("closed-view"))), res_closed
    return checked + 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worker", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--rounds", type=int, default=100)
    args = parser.parse_args()
    assert args.rounds > 0
    with tempfile.TemporaryDirectory(prefix="indexed-spaces-") as directory:
        root = Path(directory)
        path = root / "store"
        path.mkdir(mode=0o700)
        worker = Worker(args.worker.resolve(), ROOT / "tests/support/indexed_space_fixture.metta",
                        "indexed-fixture:run", language="petta", profile=True)
        server = None
        client = None
        try:
            specs = (E("SpaceIndexV1", 3, 0), E("SpaceIndexV1", 3, 1), E("SpaceIndexV1", 1, 0))
            malformed = [S("not-a-list"), Var(0), (E("SpaceIndexV1", 0, 0),),
                         (E("SpaceIndexV1", 3, 3),), (E("SpaceIndexV1", 3, -1),),
                         (E("SpaceIndexV1", 2.5, 0),), (E("SpaceIndexV1", E("danger"), 0),),
                         (E("SpaceIndexV1", 3, Var(0)),), (specs[0], specs[0])]
            for index, bad in enumerate(malformed):
                invalid_path = root / f"invalid-{index}"
                invalid_path.mkdir(mode=0o700)
                refused = worker.call(E("Create", str(invalid_path), bad))
                assert refused[0] == S("SpaceRefusedV1"), refused
                # A failed declaration publishes neither a collection nor a
                # receipt that would collide with this valid creation.
                assert worker.call(E("CreatePlain", str(invalid_path)))[0] == S("KnowledgeCommittedV1")
            orphan_path = root / "orphan"
            orphan_path.mkdir(mode=0o700)
            assert worker.call(E("OrphanIndex", str(orphan_path), False)) == E("OrphanIndexWritten", 1)
            assert worker.call(E("Create", str(orphan_path), specs)) == E("SpaceRefusedV1", S("orphaned-index-postings"))
            assert worker.call(E("At", str(orphan_path), E("Info")))[2] == 1
            assert worker.call(E("OrphanIndex", str(orphan_path), True)) == E("OrphanIndexWritten", 2)
            assert worker.call(E("Create", str(orphan_path), specs))[0] == S("KnowledgeCommittedV1")
            created = worker.call(E("Create", str(path), specs))
            assert created[0] == S("KnowledgeCommittedV1"), created
            def call(command):
                return worker.call(E("At", str(path), command))
            values = [E("edge", S("alice"), S("bob")), E("edge", S("alice"), S("bob")),
                      E("edge", S("bob"), S("carol")), E("edge", Var(0), Var(0)),
                      E("whole", Var(0)), E("edge", 0.0, S("zero")), E("edge", -0.0, S("signed-zero")),
                      E("edge", "alice", S("text")), E("edge", E("nested", S("value")), S("nested")),
                      E("danger"), E("=", E("inert-rule"), E("println!", S("ACCIDENTAL_EXECUTION"))),
                      Var(0), E("edge", "a\0b", S("nul")), E("edge", S("+"), S("native-operator")),
                      E("edge", True, S("native-true")), E("edge", False, S("native-false")),
                      E("edge", S("True"), S("capital-true")), E("edge", S("False"), S("capital-false")),
                      E("edge", S("true"), S("lower-true")), E("edge", S("false"), S("lower-false"))]
            values += [E("noise", S(f"subject-{i}"), S(f"object-{i}")) for i in range(256)]
            # Multiple bounded commits keep application receipts inside CAV1.
            references = []
            for first in range(0, len(values), 32):
                changes = tuple(E("SpaceInsertV1", i, value) for i, value in enumerate(values[first:first + 32]))
                answer = call(E("Commit", f"seed-{first}", changes))
                assert answer[0] == S("KnowledgeCommittedV1"), answer
                references += list(answer[1][1])
            assert len(references) == len(values)
            x, y = Var(0), Var(1)
            probes = [(E("edge", S("alice"), x), x), (E("edge", x, x), E("pair", x, x)),
                      ((x, S("alice"), y), (x, y)), (E("edge", "alice", x), x),
                      (E("edge", "a\0b", x), x), ((x, 0.0, y), (x, y)),
                      (E("edge", E("nested", S("value")), x), x),
                      (E(",", E("edge", S("alice"), x), E("edge", x, y)), E("pair", x, y)),
                      (E("danger"), E("danger")), (E("missing", S("absent"), x), x),
                      (E("edge", S("+"), x), x), (E("=", E("inert-rule"), x), x)]
            probes += [(E("edge", value, x), x) for value in [True, False, S("True"), S("False"), S("true"), S("false")]]
            comparisons = []
            for probe_number, (pattern, template) in enumerate(probes):
                actual = call(E("Query", pattern, template))
                reference = call(E("Reference", pattern, template))
                try:
                    raw = call(E("Raw", pattern, template))
                except RuntimeError as error:
                    raise AssertionError((probe_number, pattern, template, "raw Source boundary refused")) from error
                assert actual[0] == S("IndexedRows") and reference[0] == S("ReferenceRows"), (actual, reference)
                assert bag(row[1] for row in actual[1]) == bag(reference[1]) == bag(raw[1]), (actual, reference, raw)
                assert actual[2][0] == S("StoreQueryInfo") and actual[2][5] is False, actual
                comparisons.append({"answers": len(actual[1]), "scans": actual[2][1],
                                    "selected_indexes": actual[2][2], "candidates": actual[2][3],
                                    "decoded_bytes": actual[2][4]})
            assert comparisons[0]["candidates"] < 24 and comparisons[0]["selected_indexes"] > 0
            assert comparisons[5]["candidates"] == len(values), comparisons[5]
            update = (E("SpaceUpdateV1", references[0], E("edge", S("dave"), S("updated"))),
                      E("SpaceDeleteV1", references[1]),
                      E("SpaceUpdateV1", references[3], E("edge", S("fixed"), S("fixed"))))
            answer = call(E("Commit", "edit", update))
            assert answer[0] == S("KnowledgeCommittedV1"), answer
            for subject in [S("alice"), S("dave"), S("fixed")]:
                actual = call(E("Query", E("edge", subject, x), x))
                reference = call(E("Reference", E("edge", subject, x), x))
                assert bag(row[1] for row in actual[1]) == bag(reference[1]), (actual, reference)
            assert call(E("Commit", "edit", update))[0] == S("KnowledgeReplayedV1")
            before_info = call(E("Info"))
            bad_changes = (E("SpaceInsertV1", 0, E("would-be-partial", S("alice"), S("bad"))), E("danger"))
            refused = call(E("Commit", "invalid-tail", bad_changes))
            assert refused == E("SpaceRefusedV1", S("edit-format")), refused
            assert call(E("Info")) == before_info
            # The native key format bounds complete posting keys. A scalar
            # whose encoded posting is too wide refuses atomically; it must
            # not publish an unindexed occurrence or a success receipt.
            wide_changes = (E("SpaceInsertV1", 0, E("edge", "x" * 70000, S("too-wide"))),)
            limited = call(E("Commit", "wide-posting", wide_changes))
            assert limited == E("StoreRefused", S("invalid")), limited
            assert call(E("Info")) == before_info
            plain_wide = root / "plain-wide"
            plain_wide.mkdir(mode=0o700)
            assert worker.call(E("CreatePlain", str(plain_wide)))[0] == S("KnowledgeCommittedV1")
            assert worker.call(E("At", str(plain_wide), E("Commit", "wide-plain", wide_changes)))[0] == S("KnowledgeCommittedV1")
            plain_result = worker.call(E("At", str(plain_wide), E("Query", E("edge", "x" * 70000, x), x)))
            assert tuple(row[1] for row in plain_result[1]) == (S("too-wide"),), plain_result
            failed = call(E("Query", E("would-be-partial", S("alice"), x), x))
            assert len(failed[1]) == 1 and isinstance(failed[1][0][1], Var), failed  # whole-variable occurrence only
            # The original insertion receipt survives deletion. Replaying it
            # must not restore the deleted occurrence or its old postings.
            original = tuple(E("SpaceInsertV1", i, value) for i, value in enumerate(values[:32]))
            assert call(E("Commit", "seed-0", original))[0] == S("KnowledgeReplayedV1")
            assert call(E("Info")) == before_info
            # The persistent service consumes the same descriptor and indexes.
            server = Server(args.worker.resolve(), root); server.start(); client = Client(server.path)
            view, metadata = client.snapshot()
            query = client.query(view, E("edge", S("dave"), x), x)
            before = client.drain(query)
            info = client.route("QueryInfo", (), query)
            assert info[0] == S("StoreQueryInfo") and info[2] > 0 and info[3] < 24, info
            client.drop(query)
            # A later commit retires its own postings; this captured root keeps
            # its original postings, facts and occurrence references coherent.
            answer = call(E("Commit", "change-dave", (E("SpaceUpdateV1", references[0], E("edge", S("eve"), S("new"))),)))
            assert answer[0] == S("KnowledgeCommittedV1"), answer
            query = client.query(view, E("edge", S("dave"), x), x)
            assert client.drain(query) == before
            client.drop(query)
            assert client.route("Compact")[0] == S("StoreCompacted")
            query = client.query(view, E("edge", S("dave"), x), x)
            assert client.drain(query) == before
            client.drop(query); client.drop(view)
            client.close(); client = None
            server.stop(crash=True)
            server.start(); client = Client(server.path)
            view, metadata = client.snapshot()
            query = client.query(view, E("edge", S("eve"), x), x)
            restored = client.drain(query)
            expected = call(E("Reference", E("edge", S("eve"), x), x))
            assert bag(row[0] for row in restored) == bag(expected[1])
            assert any(row[1][0] == references[0] for row in restored), restored
            backup_root = root / "backup"
            backup_root.mkdir(mode=0o700)
            (backup_root / "store").mkdir(mode=0o700)
            assert client.route("Backup", E("BackupV1", str(backup_root / "store")), view)[0] == S("StoreBackedUp")
            copied_server = Server(args.worker.resolve(), backup_root)
            copied_client = None
            try:
                copied_server.start(); copied_client = Client(copied_server.path)
                copied_view, copied_metadata = copied_client.snapshot()
                assert copied_metadata == metadata
                copied_query = copied_client.query(copied_view, E("edge", S("eve"), x), x)
                assert copied_client.drain(copied_query) == restored
                assert copied_client.route("QueryInfo", (), copied_query)[2] > 0
                copied_client.drop(copied_query); copied_client.drop(copied_view)
            finally:
                if copied_client: copied_client.close()
                copied_server.stop()
            client.drop(query); client.drop(view)
            samples = []
            for index in range(64 + args.rounds):
                view, _ = client.snapshot()
                query = client.query(view, E("edge", S("eve"), x), x)
                if index % 2:
                    assert client.drain(query) == restored
                    assert client.route("QueryInfo", (), query)[2] > 0
                else:
                    assert client.route("Cancel", (), query) == S("StoreQueryCancelled")
                    assert client.route("Next", E("NextV1", 0), query) == S("StoreQueryCancelled")
                client.drop(query); client.drop(view)
                if index % 5 == 0:
                    abandoned = Client(server.path)
                    try:
                        abandoned_view, _ = abandoned.snapshot()
                        abandoned.query(abandoned_view, E("edge", S("eve"), x), x)
                    finally:
                        abandoned.close()
                    await_resources(client, 0)
                if index == 63 or (index >= 64 and ((index - 63) % 50 == 0 or index == args.rounds + 63)):
                    samples.append(server.sample(client, index - 63))
            assert len({sample["descriptors"] for sample in samples}) == 1, samples
            assert len({sample["stats"][2] for sample in samples}) == 1, samples
            # Historical V2 indexes may be logically incomplete despite valid
            # file checksums. The reader must scan facts, and edits must wait
            # for an explicit index upgrade instead of asserting V3 validity.
            before_legacy = call(E("Query", E("edge", S("eve"), x), x))
            assert call(E("LegacyMissingV2"))[0] == S("LegacyMissingV2")
            legacy_info = call(E("Info"))
            legacy = call(E("Query", E("edge", S("eve"), x), x))
            assert bag(row[1] for row in legacy[1]) == bag(row[1] for row in before_legacy[1]), legacy
            assert legacy[2][1] > 0 and legacy[2][2] == 0, legacy
            refused = call(E("Commit", "legacy-write", (E("SpaceInsertV1", 0, E("legacy-no-write")),)))
            assert refused == E("SpaceRefusedV1", S("index-upgrade-required")), refused
            assert call(E("Info")) == legacy_info
            equiv_checks = check_index_operations_equivalence(worker, root)
            report = {"facts": len(values), "independent_pattern_comparisons": len(probes),
                      "comparisons": comparisons, "service_info": info[1:],
                      "atomic_update_delete_replay": True, "captured_index_survives_compaction": True,
                      "restart_preserves_occurrences_and_indexes": True,
                      "backup_restore_preserves_index_access": True,
                      "legacy_v2_scan_only": True, "legacy_v2_edits_refused": True,
                      "index_operations_equivalence_checks": equiv_checks,
                      "post_warmup_indexed_service_rounds": args.rounds, "lifetime_samples": samples}
            if args.output: args.output.write_text(json.dumps(report, indent=2) + "\n")
            print(json.dumps(report, indent=2))
        finally:
            if client: client.close()
            if server: server.stop()
            worker.close()


if __name__ == "__main__":
    main()
