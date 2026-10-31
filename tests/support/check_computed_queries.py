"""Native memoized row derivations over actual captured ordinary Spaces."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / "tools")]
from space_service_client import E, S, Var, encode
from value_worker_client import Worker
from check_native_kb_service import Server, Client, await_resources
from check_space_query import rows, bag, open_query, drain, use


def counters(answer):
    return {key.name: value for key, value in answer}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("worker", type=Path)
    p.add_argument("--rounds", type=int, default=100)
    p.add_argument("--cli", type=Path)
    p.add_argument("--module", type=Path, default=ROOT / "examples/kb/space_computed_curriculum.metta")
    p.add_argument("--server-module", type=Path, default=ROOT / "tests/support/computed_query_server.metta")
    p.add_argument("--output", type=Path)
    args = p.parse_args()
    assert args.rounds > 0
    checks = []
    with tempfile.TemporaryDirectory(prefix="computed-query-") as directory:
        root = Path(directory); path = root / "store"; path.mkdir(mode=0o700)
        worker = Worker(args.worker.resolve(), args.module.resolve(), "space-computed:run", language="petta", profile=True)
        server = Server(args.worker.resolve(), root, module=args.server_module.resolve(), entry="computed-test:configuration")
        peers = []
        def connect():
            client = Client(server.path); peers.append(client); return client
        def stats(client=None):
            return counters(client.route("ComputedStats") if client else worker.call(S("Stats")))
        def embedded(count=1, fuel=2):
            return rows(worker.call(E("Query", str(path), count, fuel)))
        def remote(client, count=1, fuel=2):
            view, _ = client.snapshot(); token = open_query(client, view, plan); client.drop(view)
            return drain(client, token, count, fuel)[0]
        def read_open(client):
            view, _ = client.snapshot()
            answer = client.route("RelOpenRead", E("RelOpenV1", plan, 131072), view)
            client.drop(view); assert answer[0] == S("ServiceResourceV1"), answer
            return answer[1]
        def read_page(client, token, count=1, fuel=2):
            answer = use(client, token, "RelReadPage", E("RelPageV1", count, fuel))
            assert answer[0] == S("ServiceResourceV1"), answer
            return answer[1], answer[2]
        def complete(client, token):
            result = []
            for _ in range(10000):
                token, page = read_page(client, token)
                result.extend(page[1])
                if isinstance(page[2], tuple) and page[2][0] == S("Complete"):
                    return token, result
                assert page[2] in (S("More"), S("Paused")), page
            raise AssertionError("finite computed query did not complete")
        def changes(client, request, edits):
            view, _ = client.snapshot()
            answer = client.route("Commit", E("CommitV1", S("curriculum"), request, tuple(edits)), view)
            client.drop(view); assert answer[0] == S("KnowledgeCommittedV1"), answer
            return answer
        def decision(client, token, request):
            return use(client, token, "RelReadCommit", E("RelReadCommitV1", S("curriculum"), request,
                E("ComputedDecisionV1", request), (E("SpaceInsertV1", 0, E("computed-decision", request)),)))
        def settle(client):
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                value = client.call(E("StatsV1"))
                if value[1:3] == (1, 0): return
                time.sleep(.005)
            raise AssertionError(("unsettled clients/resources", value))
        def expected_from_rows(raw, weight):
            def count(body):
                if body[0] == S("Use"): return weight
                if body[0] == S("Seq"): return 1 + count(body[1]) + count(body[2])
                return E("ComputedSyntaxRefusedV1", body)
            return [E("ComputedProgramV1", fact[1], fact[2], count(fact[3]), refs) for fact, refs in raw]
        def stored_syntax(client):
            view, _ = client.snapshot()
            cursor = client.route("Query", E("QueryV1", S("curriculum"), E("program-syntax", Var(0), Var(1), Var(2)),
                                           E("program-syntax", Var(0), Var(1), Var(2))), view)
            assert cursor[0] == S("ServiceResourceV1"), cursor
            result = client.drain(cursor[1]); client.drop(cursor[1]); client.drop(view)
            # Used before introducing open data; later queries intentionally
            # validate ground stored rows before binding their schema.
            return result
        try:
            assert worker.call(E("Install", 42)) == E("KbRefused", S("curriculum-path"))
            assert worker.call(E("Install", "")) == E("StoreRefused", S("invalid-open"))
            assert worker.call(E("Install", str(root / "absent"))) == E("StoreRefused", S("absent"))
            checks.append("installer-shape-and-store-refusals-remain-explicit")
            assert worker.call(E("Install", str(path)))[0] == S("KnowledgeCommittedV1")
            plan = worker.call(S("Plan"))
            initial = embedded(); first_stats = stats()
            assert len(initial) == 3 and sorted(row[3] for row in initial) == [1, 3, 3], initial
            assert len({encode(row[4][0]) for row in initial}) == 3
            assert initial[0][:4] == initial[1][:4] and initial[0][4] != initial[1][4]
            assert first_stats.get("cache_miss", 0) > 0 and first_stats.get("cache_hit", 0) > 0, first_stats
            repeated = embedded(2, 8); second_stats = stats()
            assert bag(repeated) == bag(initial)
            assert second_stats.get("cache_miss", 0) == first_stats.get("cache_miss", 0), (first_stats, second_stats)
            assert second_stats.get("cache_hit", 0) >= first_stats.get("cache_hit", 0) + 3
            checks.append("native-memo-hits-with-distinct-row-occurrences")

            server.start(); first, other = connect(), connect()
            assert bag(initial) == bag(expected_from_rows(stored_syntax(first), 1))
            assert bag(remote(first)) == bag(initial)
            before = stats(first); assert bag(remote(first, 2, 8)) == bag(initial); after = stats(first)
            assert after.get("cache_hit", 0) > before.get("cache_hit", 0) and after.get("cache_miss", 0) == before.get("cache_miss", 0)
            checks.append("embedded-and-service-cache-and-logical-equivalence")

            held = read_open(first); held, paused = read_page(first, held, fuel=0)
            assert paused == E("RelPageV1", (), S("Paused")), paused
            removed = initial[0][4][0]
            body = E("Seq", E("Use", S("parse")), E("Seq", E("Use", S("lex")), E("Use", S("check"))))
            changes(other, "syntax-update", [E("SpaceDeleteV1", removed), E("SpaceInsertV1", 0, E("program-syntax", S("syntax1"), S("emit"), body))])
            held, old = complete(first, held)
            assert bag(old) == bag(initial), old
            assert decision(first, held, "old-syntax-decision") == E("StoreRefused", S("conflict"))
            fresh = remote(first); assert sorted(row[3] for row in fresh) == [1, 3, 5], fresh
            assert bag(fresh) == bag(embedded()) == bag(expected_from_rows(stored_syntax(first), 1))
            checks.append("captured-old-data-and-new-explicit-memo-arguments")

            token, current = complete(first, read_open(first))
            assert bag(current) == bag(fresh)
            assert decision(first, token, "computed-current")[0] == S("KnowledgeCommittedV1")
            checks.append("completed-derived-answer-informs-existing-writer")

            held = read_open(first)
            for _ in range(100):
                held, page = read_page(first, held, fuel=32)
                if page[1]: break
            assert page[1] and page[2] in (S("More"), S("Paused")), page
            before_code = stats(first)
            assert other.route("ComputedEdit") == S("ProgramEditedV1")
            failed = use(first, held, "RelReadPage", E("RelPageV1", 1, 128))
            assert failed[0] == S("RelPageV1") and failed[2][0] == S("Failed") and failed[2][1] == E("StoreRefused", S("program-invalidated")), failed
            await_resources(first, 0)
            changed_code = remote(first)
            assert sorted(row[3] for row in changed_code) == [2, 5, 8], changed_code
            assert bag(changed_code) == bag(expected_from_rows(stored_syntax(first), 2))
            assert stats(first).get("cache_miss", 0) > before_code.get("cache_miss", 0)
            checks.append("program-watch-and-transitive-native-memo-invalidation")

            inert = E("Use", E("println!", S("ACCIDENTAL_EXECUTION")))
            invalid = E("=", E("stored-program"), E("println!", S("ACCIDENTAL_EXECUTION")))
            changes(other, "extra-syntax", [
                E("SpaceInsertV1", 0, E("program-syntax", S("inert"), S("emit"), inert)),
                E("SpaceInsertV1", 1, E("program-syntax", S("invalid"), S("check"), invalid)),
                E("SpaceInsertV1", 2, E("program-syntax", Var(0), Var(1), E("Use", Var(2))))])
            values = remote(first)
            assert len(values) == 5, values
            assert next(row for row in values if row[1] == S("inert"))[3] == 2
            assert next(row for row in values if row[1] == S("invalid"))[3] == E("ComputedSyntaxRefusedV1", invalid)
            checks.append("open-data-excluded-before-binding-and-stored-code-remains-inert")

            prior = first.route("Resolve", E("ResolveV1", "computed-current", E("ComputedDecisionV1", "computed-current")))
            for peer in peers: peer.close()
            peers.clear(); server.stop(crash=True); server.start(); first = connect()
            assert first.route("Resolve", E("ResolveV1", "computed-current", E("ComputedDecisionV1", "computed-current"))) == prior
            assert stats(first) == {}, stats(first)
            recovered = remote(first)
            assert len(recovered) == 5
            recovered_by_id = {encode(row[4][0]): row for row in recovered}
            assert set(recovered_by_id) == {encode(row[4][0]) for row in values}
            assert sorted(row[3] for row in recovered if type(row[3]) is int) == [1, 1, 3, 5]
            worker.close(); worker = Worker(args.worker.resolve(), args.module.resolve(), "space-computed:run", language="petta", profile=True)
            assert worker.call(E("Install", str(path)))[0] == S("KnowledgeReplayedV1")
            assert bag(embedded()) == bag(recovered)
            checks.append("restart-restores-facts-identities-and-receipts-not-cache-effects")

            samples = []
            for index in range(args.rounds + 24):
                if index % 3 == 0:
                    token = read_open(first)
                    use(first, token, "RelReadCancel")
                elif index % 3 == 1:
                    assert bag(remote(first)) == bag(recovered)
                else:
                    abandoned = Client(server.path); read_open(abandoned); abandoned.close()
                settle(first)
                if index >= 23 and ((index - 23) % 25 == 0 or index == args.rounds + 23):
                    samples.append(server.sample(first, index - 23))
            assert len({sample["descriptors"] for sample in samples}) == 1, samples
            assert len({sample["stats"][2] for sample in samples}) == 1, samples
            report = dict(checks=checks, initial_values=[1, 3, 3], after_data=[1, 3, 5], after_code=[2, 5, 8],
                          first_memo_stats=first_stats, repeat_memo_stats=second_stats,
                          rounds=args.rounds, samples=samples,
                          scope="Pure row-derived memoization; no dynamic-world recursive graph tabling claim")
            if args.cli:
                # Completion status alone permits an empty answer. Require the
                # actual lesson value, its bag and IDs across fresh processes.
                sys.path.insert(0, str(ROOT / "scripts"))
                from gen_semiring_fixtures import parse_forms
                with tempfile.TemporaryDirectory(prefix="computed-cli ") as directory:
                    cli_path = Path(directory) / "store"; cli_path.mkdir(mode=0o700)
                    answers = []
                    for _ in range(2):
                        result = subprocess.run([str(args.cli.resolve()), "--lang", "petta", "--run-contract",
                            "examples/kb/space_computed_run.metta", str(cli_path)],
                            cwd=ROOT, capture_output=True, text=True, timeout=45)
                        assert result.returncode == 0 and re.fullmatch(
                            r"run: status 0; queries 4; unsettled 0\n", result.stderr), result
                        forms = parse_forms(result.stdout)
                        lesson = [form for form in forms if isinstance(form, list) and form and form[0] == "ComputedLessonV1"]
                        assert len(lesson) == 1 and lesson[0][1][0] == "KqPages", forms
                        values = [row for page in lesson[0][1][1] for row in page]
                        assert sorted(int(row[3]) for row in values) == [1, 3, 3], values
                        assert len({repr(row[4][0]) for row in values}) == 3
                        answers.append(lesson[0])
                    assert answers[0] == answers[1]
                    refused = subprocess.run([str(args.cli.resolve()), "--lang", "petta", "--run-contract",
                        "examples/kb/space_computed_run.metta", str(Path(directory) / "absent")],
                        cwd=ROOT, capture_output=True, text=True, timeout=45)
                    assert refused.returncode == 0 and re.fullmatch(
                        r"run: status 0; queries 4; unsettled 0\n", refused.stderr), refused
                    forms = parse_forms(refused.stdout)
                    assert ["StoreRefused", "absent"] in forms and not any(
                        isinstance(form, list) and form and form[0] == "ComputedLessonV1" for form in forms), forms
                report["cli_fresh_process_reopen_and_space_in_path"] = True
            if args.output: args.output.write_text(json.dumps(report, indent=2) + "\n")
            print(json.dumps(report, indent=2))
        finally:
            for peer in peers: peer.close()
            server.stop(); worker.close()


if __name__ == "__main__": main()
