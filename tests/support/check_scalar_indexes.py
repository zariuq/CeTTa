"""Versioned scalar postings share the ordinary collection commit/reindex domain."""
import argparse
import json
import math
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from value_worker_client import Worker
from space_service_client import E, S, Var, FloatBits, BigInt, Rational, encode, decode


def raw(value):
    return value.encode("utf-8") if isinstance(value, str) else value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worker", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="scalar-indexes-") as directory:
        path = Path(directory) / "store"
        path.mkdir(mode=0o700)
        workers = []
        def worker(module, entry):
            instance = Worker(args.worker.resolve(), ROOT / "tests/support" / module, entry, language="petta")
            workers.append(instance)
            return instance
        db = worker("indexed_space_fixture.metta", "indexed-fixture:run")
        storage = worker("store_fixture.metta", "store-fixture:run")
        maintenance = worker("durable_reindex_fixture.metta", "reindex-fixture:run")
        range_query = worker("space_query_fixture.metta", "space-query:run")
        metadata_key = b"space/meta/v1/" + encode(S("sample"))
        range_prefix = b"space/range-index/v1/" + encode(E("SpaceRangeIndexKeyV1", S("sample"), 3, 2))
        references = []
        try:
            def call(command):
                return db.call(E("At", str(path), command))
            def read(key):
                result = storage.call(E("Read", str(path), key, 1048576))
                assert result[0] == S("StoreValue"), result
                return raw(result[1])
            def keys():
                return {raw(key): size for key, size in storage.call(E("Keys", str(path), S("NoBound"), S("NoBound")))}
            def change(operations):
                answer = storage.call(E("Apply", str(path), E("reads"), E("writes", E("prefix", b"")), E("ops", *operations)))
                assert answer[0] == S("Applied"), answer
            def postings():
                return {key for key in keys() if key.startswith(range_prefix + b"/")}
            def expected(records):
                result = set()
                for reference, atom in records:
                    if not isinstance(atom, tuple) or len(atom) != 3:
                        continue
                    value = atom[2]
                    if isinstance(value, (tuple, Var)):
                        continue
                    tag = encode(value)[4:5]
                    if tag not in (b"I", b"F", b"J", b"Q", b"Y", b"N", b"S", b"T"):
                        continue
                    family = b"i64/" + (value + (1 << 63)).to_bytes(8, "big") + b"/" if type(value) is int else b"other/"
                    result.add(range_prefix + b"/" + family + raw(reference[2]))
                return result
            specs = (E("SpaceIndexV1", 3, 0), E("SpaceIndexV1", 3, 2), E("SpaceRangeIndexV1", 3, 2))
            created = db.call(E("Create", str(path), specs))
            assert created[0] == S("KnowledgeCommittedV1"), created
            assert decode(read(metadata_key))[0] == S("StoreSpaceV4")
            values = [E("score", S("duplicate"), -2), E("score", S("duplicate"), -2),
                      E("score", S("zero"), 0), E("score", S("float"), FloatBits.of(0.0)),
                      E("score", S("nan"), FloatBits.of(math.nan)),
                      E("score", S("big"), BigInt(str(1 << 100))), E("score", S("rational"), Rational("1/2")),
                      E("score", S("bool"), True), E("score", S("symbol"), S("true")),
                      E("score", S("text"), "a\x00ž"), E("score", S("expression"), E("compound", 1)),
                      E("score", S("open"), Var(0)), Var(0)]
            inserted = call(E("Commit", "seed", tuple(E("SpaceInsertV1", i, atom) for i, atom in enumerate(values))))
            assert inserted[0] == S("KnowledgeCommittedV1"), inserted
            references = list(inserted[1][1])
            records = list(zip(references, values))
            observed, wanted = postings(), expected(records)
            def diagnostic(items):
                return sorted((int.from_bytes(item[-8:], "little"), "integer" if b"/i64/" in item else "other") for item in items)
            assert observed == wanted, {"extra": diagnostic(observed - wanted), "missing": diagnostic(wanted - observed)}
            assert len(postings()) == 10
            # Existing ordinary matching keeps the whole-variable record,
            # independently of the range projection's omitted open records.
            ordinary = call(E("Query", E("score", S("zero"), 0), S("found")))
            assert len(ordinary[1]) == 2, ordinary
            assert ordinary[2][2] > 0, ordinary
            before = keys()
            moves = (E("SpaceUpdateV1", references[0], E("score", S("duplicate"), "moved")),
                     E("SpaceUpdateV1", references[9], E("score", S("text"), 5)),
                     E("SpaceDeleteV1", references[1]))
            updated = call(E("Commit", "move", moves))
            assert updated[0] == S("KnowledgeCommittedV1"), updated
            records[0] = (references[0], E("score", S("duplicate"), "moved"))
            records[9] = (references[9], E("score", S("text"), 5))
            del records[1]
            assert postings() == expected(records)
            assert len(postings()) == 9
            assert call(E("Commit", "move", moves))[0] == S("KnowledgeReplayedV1")
            assert call(E("Commit", "move", ()))[1] == S("receipt-collision")
            # Original fact bytes/identities survive paged rebuild; only
            # deliberately damaged derived postings and metadata are rebuilt.
            fact_bytes = {raw(ref[2]): read(raw(ref[2])) for ref, _ in records}
            wanted = postings()
            change(tuple(E("remove", key) for key in wanted))
            assert not postings()
            rebuilt = maintenance.call(E("At", str(path), E("Reindex", "repair", 8 * 1048576, 1)))
            assert rebuilt[0] == S("KnowledgeCommittedV1"), rebuilt
            assert postings() == wanted
            assert {key: read(key) for key in fact_bytes} == fact_bytes
            assert decode(read(metadata_key))[0] == S("StoreSpaceV4")
            assert maintenance.call(E("At", str(path), E("Reindex", "repair", 8 * 1048576, 1)))[0] == S("KnowledgeReplayedV1")
            full_range = E("KqRange", S("sample"), 3, 2, S("NoBound"), S("NoBound"), Var(0), S("hit"))
            fact_prefix = b"space/rows/v1/" + encode(S("sample")) + b"/"
            old_key = raw(references[2][2])
            wrong_value = range_prefix + b"/i64/" + (999 + (1 << 63)).to_bytes(8, "big") + b"/" + old_key
            missing = range_prefix + b"/i64/" + (1 << 63).to_bytes(8, "big") + b"/" + fact_prefix + b"missing"
            outside = range_prefix + b"/other/outside-collection"
            for broken, reason in [(wrong_value, "range-posting-value"), (missing, "range-missing-occurrence"),
                                   (outside, "range-occurrence-domain")]:
                change((E("put", broken, b""),))
                failed = range_query.call(E("At", str(path), full_range, 1, 2))
                assert failed[0] == S("KqFailedPages") and failed[2] == E("KbRefused", S(reason)), failed
                change((E("remove", broken),))
            existing = sorted(wanted)[0]
            change((E("put", existing, b"not-empty"),))
            failed = range_query.call(E("At", str(path), full_range, 1, 2))
            assert failed[0] == S("KqFailedPages") and failed[2] == E("KbRefused", S("range-posting-format")), failed
            change((E("put", existing, b""),))
            metadata = decode(read(metadata_key))
            downgraded = E("StoreSpaceV3", *metadata[1:])
            change((E("put", metadata_key, encode(downgraded)),))
            refused = call(E("Query", Var(0), Var(0)))
            assert refused == E("StoreRefused", S("space-metadata-format")), refused
            refused = call(E("Commit", "bad-version", ()))
            assert refused == E("SpaceRefusedV1", S("metadata-format")), refused
            change((E("put", metadata_key, encode(metadata)),))
            assert call(E("Query", E("score", S("zero"), 0), S("found")))[0] == S("IndexedRows")
            # A range-only V4 collection remains fully readable by ordinary
            # matching, using the complete fallback rather than numeric keys.
            second = Path(directory) / "range-only"
            second.mkdir(mode=0o700)
            assert db.call(E("Create", str(second), (E("SpaceRangeIndexV1", 3, 2),)))[0] == S("KnowledgeCommittedV1")
            assert db.call(E("At", str(second), E("Commit", "seed", (E("SpaceInsertV1", 0, values[0]),))))[0] == S("KnowledgeCommittedV1")
            fallback = db.call(E("At", str(second), E("Query", Var(0), S("present"))))
            assert len(fallback[1]) == 1 and fallback[2][1] == 1 and fallback[2][2] == 0, fallback
            result = {"stored_facts": len(values), "initial_range_postings": 10, "after_updates": 9,
                      "mixed_exact_range_same_projection": True, "ordinary_variable_membership_preserved": True,
                      "atomic_family_moves_delete_receipt": True, "paged_reindex_preserves_values_ids": True,
                      "wrong_metadata_version_refused": True, "range_only_ordinary_scan_fallback": True,
                      "corrupt_extra_value_missing_domain_and_nonempty_postings_refused": True,
                      "scope": "posting maintenance, ordinary reads and corruption controls; broader range qualification separate"}
            if args.output:
                args.output.write_text(json.dumps(result, indent=2) + "\n")
            print(json.dumps(result, indent=2))
        finally:
            for instance in reversed(workers):
                instance.close()


if __name__ == "__main__":
    main()
