"""Compositional joins over ordinary captured Spaces, embedded and served."""
import argparse
from collections import Counter
import json
from pathlib import Path
import random
import sqlite3
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from space_service_client import E, S, Var, encode
from value_worker_client import Worker
from check_native_kb_service import Server, Client, await_resources


def bag(rows):
    return Counter(map(encode, rows))


def rows(answer):
    assert answer[0] == S("KqPages"), answer
    return [value for page in answer[1] for value in page]


def use(client, token, operation, argument=(), **kwargs):
    return client.call(E("UseValueV1", token, S(operation), argument), **kwargs)


def open_query(client, view, plan, maximum=131072):
    answer = client.route("RelOpen", E("RelOpenV1", plan, maximum), view)
    assert answer[0] == S("ServiceResourceV1"), answer
    return answer[1]


def drain(client, token, count=3, fuel=4, *, replay=False):
    output = []
    for i in range(20000):
        body = E("UseValueV1", token, S("RelPage"), E("RelPageV1", count, fuel))
        answer = client.call(body, f"rel-page-{i}")
        if replay:
            assert client.call(body, f"rel-page-{i}") == answer
        if answer[0] == S("RelPageV1"):
            assert answer[2][0] == S("Complete"), answer
            return output + list(answer[1]), answer[2][1]
        assert answer[0] == S("ServiceResourceV1"), answer
        token, page = answer[1:]
        assert page[0] == S("RelPageV1") and page[2] in (S("More"), S("Paused")), page
        output.extend(page[1])
    raise AssertionError("query did not complete within the fixture bound")


def scan(collection, pattern, template):
    return E("KqSpace", collection, pattern, template)


def edge_plans(collection):
    x, y, z, identity = Var(0), Var(1), Var(2), Var(3)
    left = scan(collection, E("edge", x, y), E("Edge", x, y))
    binding = E("SpaceAnswerV1", E("Edge", x, y), (identity,))
    right = scan(collection, E("edge", y, z), E("Join", identity, x, y, z))
    return {"inner": E("KqBind", left, binding, right),
            "optional": E("KqOptional", left, binding, right, E("Missing", identity, x, y)),
            "semi": E("KqSemi", left, binding, right),
            "anti": E("KqAnti", left, binding, right)}


def expected_sql(edges, references):
    db = sqlite3.connect(":memory:")
    try:
        db.execute("create table edge(id integer primary key, a, b)")
        db.executemany("insert into edge values (?,?,?)", [(i, a, b) for i, (a, b) in enumerate(edges)])
        matches = db.execute("select l.id,l.a,l.b,r.id,r.b from edge l join edge r on l.b=r.a").fetchall()
        inner = [E("SpaceAnswerV1", E("Join", references[i], a, b, c), (references[j],)) for i, a, b, j, c in matches]
        optional = [E("Missing", references[i], a, b) if j is None else
                    E("SpaceAnswerV1", E("Join", references[i], a, b, c), (references[j],))
                    for i, a, b, j, c in db.execute("select l.id,l.a,l.b,r.id,r.b from edge l left join edge r on l.b=r.a")]
        result = {"inner": inner, "optional": optional}
        for name, op in [("semi", "exists"), ("anti", "not exists")]:
            result[name] = [E("SpaceAnswerV1", E("Edge", a, b), (references[i],))
                            for i, a, b in db.execute(f"select l.id,l.a,l.b from edge l where {op}(select 1 from edge r where l.b=r.a)")]
        return result
    finally:
        db.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("worker", type=Path)
    parser.add_argument("--rounds", type=int, default=100)
    parser.add_argument("--generated", type=int, default=24)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    assert args.rounds > 0 and args.generated >= 0
    with tempfile.TemporaryDirectory(prefix="space-query-") as directory:
        root = Path(directory); (root / "store").mkdir(mode=0o700)
        server = Server(args.worker.resolve(), root, module=ROOT / "tests/support/space_query_server.metta", entry="query-test:configuration")
        worker = Worker(args.worker.resolve(), ROOT / "tests/support/space_query_fixture.metta", "space-query:run", language="petta")
        client = other = None
        comparisons = 0
        try:
            server.start(); client, other = Client(server.path), Client(server.path)
            collection = S("sample")
            specs = (E("SpaceIndexV1", 3, 0), E("SpaceIndexV1", 3, 1), E("SpaceIndexV1", 3, 2))
            def install(collection, request, values):
                created = client.route("Create", E("CreateIndexedV1", collection, f"create-{request}", specs))
                assert created[0] == S("KnowledgeCommittedV1"), created
                references = []
                for start in range(0, len(values), 32):
                    view, _ = client.snapshot()
                    answer = client.route("Commit", E("CommitV1", collection, f"seed-{request}-{start}",
                        tuple(E("SpaceInsertV1", i, value) for i, value in enumerate(values[start:start + 32]))), view)
                    client.drop(view)
                    assert answer[0] == S("KnowledgeCommittedV1"), answer
                    references.extend(answer[1][1])
                return references
            edges = [("a", "b"), ("a", "b"), ("b", "c"), ("c", "a"), ("c", "d"), ("z", "absent")]
            facts = [E("edge", a, b) for a, b in edges]
            facts += [E("noise", f"key-{i}", f"value-{i}") for i in range(96)]
            references = install(collection, "main", facts)
            expected = expected_sql(edges, references[:len(edges)])
            plans = edge_plans(collection)
            def compare(plan, expected_rows, count=3, fuel=4, replay=False):
                nonlocal comparisons
                other.call(E("StatsV1"))  # Keep an intentionally idle peer alive.
                embedded = rows(worker.call(E("At", str(root / "store"), plan, count, fuel)))
                view, _ = client.snapshot()
                token = open_query(client, view, plan)
                client.drop(view)  # The query owns an independent captured root.
                served, work = drain(client, token, count, fuel, replay=replay)
                assert bag(embedded) == bag(served) == bag(expected_rows), (plan, embedded, served, expected_rows)
                await_resources(client, 0)
                comparisons += 1
                return work
            for name, plan in plans.items():
                for count, fuel in [(1, 1), (3, 4), (32, 128)]:
                    compare(plan, expected[name], count, fuel, replay=count == 1)
            compare(E("KqUnion", (plans["inner"], plans["inner"])), expected["inner"] * 2)
            selected = scan(collection, E("edge", "a", Var(0)), Var(0))
            work = compare(selected, [E("SpaceAnswerV1", "b", (reference,)) for reference in references[:2]])
            assert work[2] < len(facts), work
            first = worker.call(E("First", str(root / "store"), selected, 1, 0, 131072))
            assert first == E("KqFirst", (), S("paused"), E("KqWork", 0, 0, 0)), first
            failure = worker.call(E("First", str(root / "store"), selected, 1, 128, 8))
            assert failure[0] == S("KqFirstFailure") and failure[2][0] == S("StoreQueryTooLarge"), failure

            # Empty projected values are present answers, not optional absence.
            for right, want in [(E("KqRows", ((),)), ((), (), ())),
                                (E("KqRows", ()), (E("NoRight", 1), E("NoRight", 1), E("NoRight", 2)))]:
                compare(E("KqOptional", E("KqRows", (1, 1, 2)), Var(0), right, E("NoRight", Var(0))), want)
            inert = (True, False, S("True"), S("False"), S("true"), S("false"),
                     E("Error", S("data"), S("value")), E("=", E("rule"), E("println!", S("ACCIDENTAL_EXECUTION"))),
                     E("pair", Var(0), Var(0)), E("pair", Var(0), Var(1)),
                     E("sq-test:unapproved", "must-stay-data"),
                     E("sq-test:no-answer", "must-stay-data"),
                     E("sq-test:many-answers", "must-stay-data"))
            compare(E("KqRows", inert), inert)
            compare(E("KqMap", E("KqRows", inert), S("sq-test:identity")), inert)
            compare(E("KqOrder", E("KqRows", inert), S("sq-test:never-less"), 131072), sorted(inert, key=encode))
            compare(E("KqSelect", E("KqRows", (E("pair", Var(0), Var(0)), E("pair", Var(0), Var(1)))), S("sq-test:bind")),
                    (E("pair", Var(0), Var(0)), E("pair", Var(0), Var(1))))
            distinct_values = (E("pair", Var(0), Var(0)), E("pair", Var(2), Var(2)), E("pair", Var(0), Var(1)))
            compare(E("KqDistinct", E("KqRows", distinct_values), S("kb:alpha-equal"), 131072), (distinct_values[0], distinct_values[2]))

            metric_values = [("a", 3), ("a", 1), ("b", -4), ("b", 0), ("a", 3)]
            metric_refs = install("metrics", "metrics", [E("metric", key, value) for key, value in metric_values])
            metrics = scan("metrics", E("metric", Var(0), Var(1)), E("Metric", Var(0), Var(1)))
            metric_rows = [E("SpaceAnswerV1", E("Metric", key, value), (ref,)) for (key, value), ref in zip(metric_values, metric_refs)]
            compare(E("KqSelect", metrics, S("sq-test:positive")), [row for row in metric_rows if row[1][2] > 0])
            compare(E("KqMap", metrics, S("sq-test:metric-value")), [E("Value", value) for _, value in metric_values])
            initial = E("Stats", 0, 0, S("NoValue"), S("NoValue"), S("NoValue"))
            stats_db = sqlite3.connect(":memory:")
            try:
                stats_db.execute("create table metric(k text,v integer)")
                stats_db.executemany("insert into metric values (?,?)", metric_values)
                def summary(row):
                    n, total, minimum, maximum, average = row
                    return E("Stats", n, total or 0, S("NoValue") if minimum is None else E("Some", minimum),
                             S("NoValue") if maximum is None else E("Some", maximum),
                             S("NoValue") if average is None else E("Some", average))
                expected_fold = summary(stats_db.execute("select count(*),sum(v),min(v),max(v),avg(v) from metric").fetchone())
                compare(E("KqFold", metrics, initial, S("sq-test:stats"), 131072), [expected_fold])
                expected_groups = [E("RelGroup", key, summary(values)) for key, *values in stats_db.execute("select k,count(*),sum(v),min(v),max(v),avg(v) from metric group by k")]
                compare(E("KqGroup", metrics, S("sq-test:metric-key"), S("kb:alpha-equal"), initial, S("sq-test:stats"), 131072), expected_groups)
                compare(E("KqFold", E("KqRows", ()), initial, S("sq-test:stats"), 131072), [initial])
            finally:
                stats_db.close()
            ordered = sorted(metric_rows, key=lambda row: (row[1][2], encode(row)))
            ordered_plan = E("KqOrder", metrics, S("sq-test:metric-less"), 131072)
            # Compare ordered sequences as sequences, not merely answer bags.
            assert rows(worker.call(E("At", str(root / "store"), ordered_plan, 2, 3))) == ordered
            view, _ = client.snapshot(); token = open_query(client, view, ordered_plan); client.drop(view)
            assert drain(client, token, 2, 3)[0] == ordered
            sliced = E("KqSlice", ordered_plan, 1, 3)
            compare(sliced, ordered[1:4], 1, 2)
            compare(E("KqSlice", metrics, 0, 0), [])
            # Explicit callback failures and bounded-state exhaustion never
            # become successful empty answers; terminal states release owners.
            failures = [E("KqMap", metrics, S("sq-test:no-answer")),
                        E("KqMap", metrics, S("sq-test:many-answers")),
                        E("KqOrder", metrics, S("sq-test:metric-less"), 16),
                        E("KqDistinct", metrics, S("kb:alpha-equal"), 16),
                        E("KqGroup", metrics, S("sq-test:metric-key"), S("kb:alpha-equal"), initial, S("sq-test:stats"), 16),
                        E("KqFold", metrics, (), S("sq-test:append"), 16)]
            for plan in failures:
                result = worker.call(E("At", str(root / "store"), plan, 3, 8))
                assert result[0] == S("KqFailedPages"), result
                view, _ = client.snapshot(); token = open_query(client, view, plan); client.drop(view)
                for _ in range(100):
                    result = use(client, token, "RelPage", E("RelPageV1", 3, 8))
                    if result[0] == S("RelPageV1"):
                        assert result[2][0] == S("Failed"), result
                        break
                    assert result[0] == S("ServiceResourceV1"), result
                    token = result[1]
                else: raise AssertionError("failed stage did not terminate")
                await_resources(client, 0)
            faulty_right = E("KqUnion", (E("KqRows", ("hit",)),
                E("KqMap", E("KqRows", ("bad",)), S("sq-test:many-answers"))))
            for mode, wanted in [("KqSemi", (1, 2)), ("KqAnti", ())]:
                compare(E(mode, E("KqRows", (1, 2)), Var(0), faulty_right), wanted)
            optional_fault = E("KqOptional", E("KqRows", (1,)), Var(0),
                E("KqMap", E("KqRows", ("bad",)), S("sq-test:no-answer")), S("Missing"))
            failed_optional = worker.call(E("At", str(root / "store"), optional_fault, 1, 8))
            assert failed_optional[0] == S("KqFailedPages") and not any(failed_optional[1]), failed_optional
            view, _ = client.snapshot()
            unapproved = E("KqMap", E("KqRows", ("attempt",)), S("sq-test:unapproved"))
            assert client.route("RelOpen", E("RelOpenV1", unapproved, 131072), view) == E("KbRefused", S("query-service-plan"))
            dynamic = E("KqBind", E("KqRows", (S("sq-test:unapproved"),)), Var(0), E("KqMap", E("KqRows", ("attempt",)), Var(0)))
            assert client.route("RelOpen", E("RelOpenV1", dynamic, 131072), view) == E("KbRefused", S("query-service-plan"))
            client.drop(view)
            assert client.route("UnapprovedCount") == 0
            # Putting a policy-looking fact in the KB grants no callback
            # standing in the server program, including after attenuation.
            view, _ = client.snapshot()
            assert client.route("Commit", E("CommitV1", collection, "policy-looking-data",
                (E("SpaceInsertV1", 0, E("QueryPureCallbackV1", S("sq-test:unapproved"))),)), view)[0] == S("KnowledgeCommittedV1")
            client.drop(view)
            readonly = Client(server.path)
            try:
                assert readonly.call(E("ReadOnlyV1")) == S("ServiceReadOnlyV1")
                view, _ = readonly.snapshot()
                assert readonly.route("RelOpen", E("RelOpenV1", unapproved, 131072), view) == E("KbRefused", S("query-service-plan"))
                readonly.drop(view)
            finally:
                readonly.close()
            assert client.route("UnapprovedCount") == 0

            open_values = (E("pair", Var(0), Var(0)), E("pair", Var(0), Var(1)), Var(0),
                           True, S("True"), E("Error", S("data"), S("value")),
                           E("=", E("stored-rule"), E("println!", S("ACCIDENTAL_EXECUTION"))))
            install("open-data", "open-data", open_values)
            for pattern, template in [(E("pair", Var(0), Var(0)), E("Bound", Var(0), Var(0))),
                                      (Var(0), Var(0)), (S("True"), ()),
                                      (E("=", Var(0), Var(1)), E("Program", Var(0), Var(1)))]:
                view, _ = client.snapshot()
                opened = client.route("Query", E("QueryV1", "open-data", pattern, template), view)
                assert opened[0] == S("ServiceResourceV1"), opened
                raw = client.drain(opened[1]); client.drop(opened[1]); client.drop(view)
                compare(scan("open-data", pattern, template), [E("SpaceAnswerV1", *row) for row in raw])

            # The common matcher, rather than a new relational unifier, handles
            # conjunction and repeated variables. Compare directly with its
            # ordinary query endpoint, including occurrence IDs and sharing.
            view, _ = client.snapshot()
            pattern = E(",", E("edge", "a", Var(0)), E("edge", Var(0), Var(1)))
            raw_query = client.query(view, pattern, E("Pair", Var(0), Var(1)))
            raw = client.drain(raw_query)
            client.drop(raw_query); client.drop(view)
            compare(scan(collection, pattern, E("Pair", Var(0), Var(1))), [E("SpaceAnswerV1", *row) for row in raw])

            rng = random.Random(41421)
            for trial in range(args.generated):
                coll = f"generated-{trial}"
                sample = [(rng.randrange(5), rng.randrange(5)) for _ in range(rng.randrange(0, 11))]
                refs = install(coll, coll, [E("edge", a, b) for a, b in sample])
                oracle = expected_sql(sample, refs)
                for name, plan in edge_plans(coll).items():
                    compare(plan, oracle[name], 2, 3)

            # Each inner scan opens against the captured root even if another
            # client publishes and compacts before that scan is reached.
            view, coordinates = client.snapshot()
            held = open_query(client, view, plans["inner"])
            deletion_view, _ = other.snapshot()
            removed = other.route("Commit", E("CommitV1", collection, "delete-edge", (E("SpaceDeleteV1", references[2]),)), deletion_view)
            other.drop(deletion_view)
            assert removed[0] == S("KnowledgeCommittedV1"), removed
            assert other.route("Compact")[0] == S("StoreCompacted")
            held_rows, _ = drain(client, held)
            assert bag(held_rows) == bag(expected["inner"])
            stale = client.route("Commit", E("CommitV1", collection, "stale-informed-write", (E("SpaceInsertV1", 0, E("decision", "stale")),)), view)
            assert stale == E("StoreRefused", S("conflict")), stale
            client.drop(view)

            # An absent inner match is a dependency too. Publishing a blocker
            # leaves old answers stable and makes a query-informed write stale.
            anti = E("KqAnti", E("KqRows", ("allowed",)), Var(0), scan(collection, E("blocked", Var(1)), Var(1)))
            view, _ = client.snapshot(); token = open_query(client, view, anti)
            write_view, _ = other.snapshot()
            assert other.route("Commit", E("CommitV1", collection, "block", (E("SpaceInsertV1", 0, E("blocked", "now")),)), write_view)[0] == S("KnowledgeCommittedV1")
            other.drop(write_view)
            assert drain(client, token)[0] == ["allowed"]
            stale = client.route("Commit", E("CommitV1", collection, "stale-negative-write", (E("SpaceInsertV1", 0, E("action", "not-published")),)), view)
            assert stale == E("StoreRefused", S("conflict")), stale
            client.drop(view)
            compare(anti, [])

            view, _ = client.snapshot(); token = open_query(client, view, selected); client.drop(view)
            assert use(other, token, "RelCancel", ok=False) == E("ServiceRefusedV1", S("unknown-resource"))
            observed = use(client, token, "RelInspect")
            assert observed[0] == S("ServiceResourceV1") and observed[2][0] == S("KqSpaceInspectionV1"), observed
            token = observed[1]
            assert client.call(E("ReadOnlyV1")) == S("ServiceReadOnlyV1")
            assert use(client, token, "RelCancel") == S("RelCancelledV1")
            client.close(); client = Client(server.path); await_resources(client, 0)
            samples = []
            lifetime_plans = [plans["optional"], ordered_plan,
                E("KqFold", metrics, initial, S("sq-test:stats"), 131072),
                E("KqGroup", metrics, S("sq-test:metric-key"), S("kb:alpha-equal"), initial, S("sq-test:stats"), 131072)]
            for index in range(args.rounds + 32):
                view, _ = client.snapshot(); token = open_query(client, view, lifetime_plans[index % len(lifetime_plans)]); client.drop(view)
                part = use(client, token, "RelPage", E("RelPageV1", 1, 5))
                assert part[0] == S("ServiceResourceV1"), part
                assert use(client, part[1], "RelCancel") == S("RelCancelledV1")
                if index % 5 == 0:
                    abandoned = Client(server.path)
                    av, _ = abandoned.snapshot(); open_query(abandoned, av, plans["inner"])
                    abandoned.close()
                await_resources(client, 0)
                if index >= 31 and ((index - 31) % 25 == 0 or index == args.rounds + 31):
                    samples.append(server.sample(client, index - 31))
            assert len({s["descriptors"] for s in samples}) == 1, samples
            assert len({s["stats"][2] for s in samples}) == 1, samples
            before_restart = rows(worker.call(E("At", str(root / "store"), plans["inner"], 3, 4)))
            server.stop(crash=True); client.close(); other.close()
            server.start(); client, other = Client(server.path), Client(server.path)
            compare(plans["inner"], before_restart)
            report = {"embedded_service_oracle_comparisons": comparisons,
                      "generated_graphs": args.generated, "generated_seed": 41421,
                      "initial_facts": len(facts), "indexed_scan_work": work[1:],
                      "joins_preserve_bags_and_occurrence_ids": True,
                      "empty_value_distinct_from_absence": True,
                      "inert_and_shared_variable_rows": True,
                      "captured_inner_scans_survive_publication_compaction": True,
                      "negative_read_informing_revision_conflicts": True,
                      "pipeline": ["map", "select", "distinct", "fold", "group", "order", "slice"],
                      "numeric_aggregates_against_sqlite": ["count", "sum", "min", "max", "average"],
                      "canonical_order_ties_and_sequence_paging": True,
                      "callback_cardinality_and_state_quota_failures": True,
                      "static_server_callback_allowlist": True,
                      "dynamic_callback_and_policy_fact_refused": True,
                      "post_warmup_cancel_rounds": args.rounds, "lifetime_samples": samples}
            if args.output: args.output.write_text(json.dumps(report, indent=2) + "\n")
            print(json.dumps(report, indent=2))
        finally:
            if client: client.close()
            if other: other.close()
            server.stop(); worker.close()


if __name__ == "__main__":
    main()
