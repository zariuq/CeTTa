"""Stored-scalar ranges through shared matching: index, scan, oracle and service."""
import argparse
from collections import Counter
from fractions import Fraction
import itertools
import json
import math
from pathlib import Path
import struct
import socket
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from value_worker_client import Worker
from space_service_client import E, S, Var, Symbol, FloatBits, BigInt, Rational, encode
from check_space_query import rows, open_query, drain, use
from check_native_kb_service import Server, Client, await_resources


def scalar(value):
    return type(value) in (int, bool, str, bytes) or isinstance(value, (Symbol, FloatBits, BigInt, Rational))


def number(value):
    if isinstance(value, FloatBits):
        f = struct.unpack("<d", struct.pack("<Q", value.bits))[0]
        if math.isnan(f):
            return (0, Fraction(0), 0)
        if math.isinf(f):
            return (1 if f < 0 else 3, Fraction(0), 0)
        return (2, Fraction.from_float(f), -2 if f == 0 and math.copysign(1, f) < 0 else -1)
    return (2, Fraction(value if type(value) is int else value.decimal if isinstance(value, BigInt) else value.spelling), 0)


def key(value):
    if type(value) is int or isinstance(value, (FloatBits, BigInt, Rational)):
        return (0, number(value))
    if isinstance(value, (str, bytes)):
        return (1, value.encode("utf-8") if isinstance(value, str) else value)
    if type(value) is bool:
        return (2, b"true" if value else b"false")
    return (2, value.name.encode("utf-8") if isinstance(value.name, str) else value.name)


def inside(value, lower, upper):
    if not scalar(value):
        return False
    if lower != S("NoBound") and (key(value) < key(lower[1]) or (lower[0] == S("Exclusive") and key(value) == key(lower[1]))):
        return False
    if upper != S("NoBound") and (key(value) > key(upper[1]) or (upper[0] == S("Exclusive") and key(value) == key(upper[1]))):
        return False
    return True


def bag(values):
    return Counter(map(encode, values))


def plan(lower, upper):
    return E("KqRange", S("sample"), 3, 2, lower, upper,
             E("score", Var(0), Var(1)), E("Result", Var(0), Var(1)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worker", type=Path)
    parser.add_argument("--rounds", type=int, default=100)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    assert args.rounds > 0
    with tempfile.TemporaryDirectory(prefix="scalar-ranges-") as directory:
        root = Path(directory)
        workers = []
        def worker(module, entry):
            result = Worker(args.worker.resolve(), ROOT / "tests/support" / module, entry, language="petta")
            workers.append(result)
            return result
        db = worker("indexed_space_fixture.metta", "indexed-fixture:run")
        query = worker("space_query_fixture.metta", "space-query:run")
        server = client = other = None
        try:
            values = [-(1 << 63), -(1 << 53) - 1, -2, -2, -1, 0, 1, 2, 4, (1 << 53), (1 << 53) + 1, (1 << 63) - 1,
                      *map(FloatBits.of, [-math.inf, -1.5, -0.0, 0.0, 0.5, 1.5, float(1 << 53), math.inf]),
                      FloatBits(0x7ff8000000000001), FloatBits(0xfff8000000000002),
                      BigInt(str(1 << 100)), Rational("-1/3"), Rational("1/2"),
                      "", "a\x00b", "ž", S("a"), S("false"), S("true"), False, True,
                      Var(0), E("compound", 1), E("sq-test:unapproved")]
            facts = [E("score", S(f"row-{i}"), value) for i, value in enumerate(values)]
            facts += [Var(0), E("=", E("stored-rule"), S("inert"))]
            paths = []
            references = []
            for indexed in (False, True):
                path = root / ("indexed" if indexed else "plain")
                path.mkdir(mode=0o700)
                created = db.call(E("Create", str(path), (E("SpaceRangeIndexV1", 3, 2),))) if indexed else db.call(E("CreatePlain", str(path)))
                assert created[0] == S("KnowledgeCommittedV1"), created
                refs = []
                for start in range(0, len(facts), 20):
                    answer = db.call(E("At", str(path), E("Commit", f"seed-{start}",
                                     tuple(E("SpaceInsertV1", i, atom) for i, atom in enumerate(facts[start:start + 20])))))
                    assert answer[0] == S("KnowledgeCommittedV1"), answer
                    refs.extend(answer[1][1])
                paths.append(path)
                references.append(refs)
            bounds = [S("NoBound"), E("Inclusive", -2), E("Exclusive", -2), E("Inclusive", 0),
                      E("Exclusive", FloatBits.of(0.0)), E("Inclusive", FloatBits.of(0.5)),
                      E("Inclusive", (1 << 53) + 1), E("Inclusive", Rational("1/2")),
                      E("Inclusive", FloatBits(0x7ff8000000000001)), E("Inclusive", "a"),
                      E("Inclusive", S("true")), E("Exclusive", True)]
            comparisons = 0
            for lower, upper in itertools.product(bounds, repeat=2):
                expected_values = [E("Result", S(f"row-{i}"), value) for i, value in enumerate(values) if inside(value, lower, upper)]
                for path, refs in zip(paths, references):
                    answer = rows(query.call(E("At", str(path), plan(lower, upper), 2, 3)))
                    expected = [E("SpaceAnswerV1", E("Result", S(f"row-{i}"), value), (refs[i],))
                                for i, value in enumerate(values) if inside(value, lower, upper)]
                    assert bag(answer) == bag(expected), (lower, upper, len(answer), len(expected), answer[:2])
                    assert bag(row[1] for row in answer) == bag(expected_values)
                    comparisons += 1
            # Empty integer window still has an eligible floating/rational
            # answer in the other-scalar bucket.
            narrow = plan(E("Exclusive", 0), E("Exclusive", 1))
            answer = rows(query.call(E("At", str(paths[1]), narrow, 1, 1)))
            assert len(answer) == 2, answer
            selected = plan(E("Inclusive", -2), E("Exclusive", 2))
            joined = E("KqBind", selected,
                       E("SpaceAnswerV1", E("Result", Var(10), Var(11)), (Var(12),)),
                       E("KqRows", (E("Carried", Var(10), Var(11), Var(12)),)))
            selected_rows = rows(query.call(E("At", str(paths[1]), selected, 2, 2)))
            joined_rows = rows(query.call(E("At", str(paths[1]), joined, 1, 1)))
            assert bag(joined_rows) == bag(E("Carried", row[1][1], row[1][2], row[2][0]) for row in selected_rows)
            ordered = E("KqOrder", selected, S("sq-test:never-less"), 131072)
            small_pages = rows(query.call(E("At", str(paths[1]), ordered, 1, 2)))
            large_pages = rows(query.call(E("At", str(paths[1]), ordered, 13, 64)))
            assert small_pages == large_pages and list(map(encode, small_pages)) == sorted(map(encode, selected_rows))
            bulk = [E("metric", S(f"key-{i}"), i) for i in range(256)]
            for path in paths:
                for start in range(0, len(bulk), 32):
                    seeded = db.call(E("At", str(path), E("Commit", f"bulk-{start:04}",
                                     tuple(E("SpaceInsertV1", i, atom) for i, atom in enumerate(bulk[start:start + 32])))))
                    assert seeded[0] == S("KnowledgeCommittedV1"), seeded
            sparse = E("KqRange", S("sample"), 3, 2, E("Inclusive", 200), E("Inclusive", 202),
                       E("metric", Var(0), Var(1)), E("Metric", Var(0), Var(1)))
            first = [query.call(E("First", str(path), sparse, 1, 4096, 131072)) for path in paths]
            assert first[0][1][0][1] == first[1][1][0][1] == E("Metric", S("key-200"), 200), first
            assert first[1][3][2] <= 4 and first[0][3][2] >= 150, first
            # A compound/open field is outside this stored-data projection,
            # even when ordinary unification could instantiate it numerically.
            for bad in [E("KqRange", S("sample"), 3, 2, E("Inclusive", Var(0)), S("NoBound"), Var(1), Var(1)),
                        E("KqRange", S("sample"), 3, 3, S("NoBound"), S("NoBound"), Var(0), Var(0)),
                        E("KqRange", S("sample"), 3, 2, S("NoBound"), S("NoBound"), E(",", E("score", Var(0), Var(1))), Var(0))]:
                try:
                    refused = query.call(E("At", str(paths[1]), bad, 1, 1))
                except RuntimeError as error:
                    raise AssertionError(("invalid range plan did not refuse", bad)) from error
                assert refused[0] in (S("StoreRefused"), S("KbRefused")), refused
            service = root / "service"
            service.mkdir(mode=0o700)
            (service / "store").mkdir(mode=0o700)
            server = Server(args.worker.resolve(), service, module=ROOT / "tests/support/program_query_server.metta", entry="program-server:configuration")
            server.start()
            client, other = Client(server.path), Client(server.path)
            assert client.route("Create", E("CreateIndexedV1", S("sample"), "create", (E("SpaceRangeIndexV1", 3, 2),)))[0] == S("KnowledgeCommittedV1")
            view, _ = client.snapshot()
            inserted = client.route("Commit", E("CommitV1", S("sample"), "seed", tuple(E("SpaceInsertV1", i, atom) for i, atom in enumerate(facts[:20]))), view)
            assert inserted[0] == S("KnowledgeCommittedV1"), inserted
            client.drop(view)
            view, _ = client.snapshot()
            service_plan = plan(E("Inclusive", -2), E("Exclusive", 2))
            token = open_query(client, view, service_plan)
            client.drop(view)
            inspected = use(client, token, "RelInspect")
            assert inspected[0] == S("ServiceResourceV1"), inspected
            token = inspected[1]
            progressed = use(client, token, "RelPage", E("RelPageV1", 1, 4))
            assert progressed[0] == S("ServiceResourceV1"), progressed
            prefix_rows = list(progressed[2][1])
            token = progressed[1]
            inspected = use(client, token, "RelInspect")
            assert inspected[0] == S("ServiceResourceV1"), inspected
            token = inspected[1]
            got, work = drain(client, token, 2, 2, replay=True)
            got = prefix_rows + got
            wanted = [E("SpaceAnswerV1", E("Result", S(f"row-{i}"), value), (inserted[1][1][i],))
                      for i, value in enumerate(values[:20]) if inside(value, service_plan[4], service_plan[5])]
            assert bag(got) == bag(wanted), (got, wanted)
            view, _ = client.snapshot()
            inert = E("KqRange", S("sample"), 3, 2, S("NoBound"), S("NoBound"),
                      E("score", Var(0), Var(1)), E("sq-test:unapproved"))
            token = open_query(client, view, inert)
            client.drop(view)
            inert_rows, _ = drain(client, token, 3, 8)
            assert len(inert_rows) == 20 and all(row[1] == E("sq-test:unapproved") for row in inert_rows), inert_rows
            assert client.route("UnapprovedCount") == 0
            view, _ = client.snapshot()
            token = open_query(client, view, service_plan)
            client.drop(view)
            page = use(client, token, "RelPage", E("RelPageV1", 1, 16))
            assert page[0] == S("ServiceResourceV1"), page
            prefix = list(page[2][1])
            token = page[1]
            view, _ = other.snapshot()
            assert other.route("Commit", E("CommitV1", S("sample"), "later-publisher", (E("SpaceInsertV1", 0, E("score", S("later"), 1)),)), view)[0] == S("KnowledgeCommittedV1")
            other.drop(view)
            assert other.route("Compact")[0] == S("StoreCompacted")
            rest, _ = drain(client, token, 2, 2)
            assert bag(prefix + rest) == bag(wanted)
            def read_context(chosen=service_plan):
                view, _ = client.snapshot()
                opened = client.route("RelOpenRead", E("RelOpenV1", chosen, 131072), view)
                client.drop(view)
                assert opened[0] == S("ServiceResourceV1"), opened
                token = opened[1]
                for _ in range(1000):
                    page = use(client, token, "RelReadPage", E("RelPageV1", 3, 16))
                    assert page[0] == S("ServiceResourceV1"), page
                    token = page[1]
                    if isinstance(page[2][2], tuple) and page[2][2][0] == S("Complete"):
                        return token
                raise AssertionError("range read did not complete")
            def decision(request):
                return E("RelReadCommitV1", S("sample"), request, E("RangeDecisionV1", request),
                         (E("SpaceInsertV1", 0, E("decision", request)),))
            token = read_context()
            assert other.route("EditProgram") == S("ProgramEditedV1")
            assert use(client, token, "RelReadCommit", decision("stale-range-code")) == E("StoreRefused", S("program-invalidated"))
            token = read_context()
            view, _ = other.snapshot()
            assert other.route("Commit", E("CommitV1", S("sample"), "new-range-value", (E("SpaceInsertV1", 0, E("score", S("new"), 1)),)), view)[0] == S("KnowledgeCommittedV1")
            other.drop(view)
            assert use(client, token, "RelReadCommit", decision("stale-range-data")) == E("StoreRefused", S("conflict"))
            token = read_context(plan(E("Inclusive", 9999), E("Inclusive", 10000)))
            view, _ = other.snapshot()
            assert other.route("Commit", E("CommitV1", S("sample"), "range-phantom", (E("SpaceInsertV1", 0, E("score", S("phantom"), 9999)),)), view)[0] == S("KnowledgeCommittedV1")
            other.drop(view)
            assert use(client, token, "RelReadCommit", decision("stale-range-absence")) == E("StoreRefused", S("conflict"))
            token = read_context()
            client.send(E("UseValueV1", token, S("RelReadCommit"), decision("range-decision")))
            client.socket.shutdown(socket.SHUT_WR)
            await_resources(other, 0)
            settled = other.route("Resolve", E("ResolveV1", "range-decision", E("RangeDecisionV1", "range-decision")))
            assert settled[0] == S("KnowledgeResolvedV1"), settled
            server.stop(crash=True)
            client.close()
            other.close()
            server.start()
            client, other = Client(server.path), Client(server.path)
            resolved = client.route("Resolve", E("ResolveV1", "range-decision", E("RangeDecisionV1", "range-decision")))
            assert resolved == settled, resolved
            token = read_context()
            assert use(client, token, "RelReadCommit", decision("range-decision"))[0] == S("KnowledgeReplayedV1")
            samples = []
            for i in range(args.rounds + 32):
                if i % 5 == 0:
                    other.call(E("StatsV1"))
                view, _ = client.snapshot()
                token = open_query(client, view, service_plan)
                client.drop(view)
                if i % 2:
                    page = use(client, token, "RelPage", E("RelPageV1", 1, 7))
                    assert page[0] == S("ServiceResourceV1"), page
                    token = page[1]
                assert use(client, token, "RelCancel") == S("RelCancelledV1")
                if i % 4 == 0:
                    abandoned = Client(server.path)
                    v, _ = abandoned.snapshot()
                    leaked = open_query(abandoned, v, service_plan)
                    abandoned.drop(v)
                    if i % 8 == 0:
                        use(abandoned, leaked, "RelPage", E("RelPageV1", 1, 7))
                    abandoned.close()
                await_resources(client, 0)
                if i >= 31 and ((i - 31) % 25 == 0 or i == args.rounds + 31):
                    samples.append(server.sample(client, i - 31))
            assert len({s["descriptors"] for s in samples}) == 1, samples
            assert len({s["stats"][2] for s in samples}) == 1, samples
            result = {"index_scan_oracle_comparisons": comparisons, "scalar_values": len(values),
                      "integer_empty_window_other_scalar_answers": 2,
                      "bindings_memberships_and_duplicate_bags": True, "invalid_bounds_and_patterns_refused": True,
                      "same_service_results_and_exact_page_replay": True,
                      "call_shaped_template_is_data_with_zero_effects": True,
                      "join_and_ordered_paging": True, "sparse_first_candidate_visits": {"scan": first[0][3][2], "index": first[1][3][2]},
                      "range_informed_code_data_validation_restart_receipt": True,
                      "captured_publish_compaction_and_absence_phantom": True,
                      "lost_commit_reply_resolved_before_restart_retry": True,
                      "post_warmup_cancel_rounds": args.rounds, "lifetime_samples": samples}
            if args.output:
                args.output.write_text(json.dumps(result, indent=2) + "\n")
            print(json.dumps(result, indent=2))
        finally:
            if client:
                client.close()
            if other:
                other.close()
            if server:
                server.stop()
            for instance in reversed(workers):
                instance.close()


if __name__ == "__main__":
    main()
