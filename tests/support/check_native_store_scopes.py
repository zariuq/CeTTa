#!/usr/bin/env python3
"""Replay scoped schedules through the MeTTa caller, SQLite and Lean.

The SQL oracle uses ordered full-record dependency views. Observations are
one-use, including rejected/no-op proposals; restart invalidates leases, not
committed records. This tests model correspondence, not C verification.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import random
import sqlite3
import subprocess
import sys

from check_native_store_transactions import ROOT, digest


def commands(seed):
    result = [
        ["observe", 0, [7], [7]], ["observe", 1, [8], [8]],
        ["commit", 0, [["add", 7], ["add", 7]]],
        ["commit", 1, [["add", 8]]],
        ["observe", 2, [9], [7]], ["external", [["add", 9]]],
        ["commit", 2, [["remove", 7]]],
        ["observe", 3, [], [7]], ["commit", 3, []], ["commit", 3, []],
        ["observe", 0, [], [7]], ["commit", 0, [["add", 8]]],
        ["commit", 0, [["remove", 7]]],
        ["observe", 1, [None], [None]], ["restart"], ["commit", 1, []],
        ["external", [["add", 1000], ["add", 1001]]],
        ["observe", 2, [], [1000, 1001]],
        ["external", [["remove", 1000], ["add", 1000]]],
        ["commit", 2, [["remove", 1000], ["add", 1000]]],
        ["observe", 2, [None], [1000, 1001]],
        ["external", [["remove", 1001], ["add", 1001]]],
        ["commit", 2, [["remove", 1000]]],
    ]
    # Include many successful interleaved commits, not just random refusals.
    for round_number in range(20):
        left, right = 100+round_number*2, 101+round_number*2
        result += [["observe", 0, [left], [left]], ["observe", 1, [right], [right]],
                   ["commit", 0, [["add", left]]], ["commit", 1, [["add", right]]]]
    rng = random.Random(seed)
    for _ in range(210):
        action = rng.choice(("observe", "observe", "external", "commit", "commit", "close"))
        operations = [[rng.choice(("add", "remove")), rng.randrange(6)] for _ in range(rng.randrange(4))]
        if action == "observe":
            result.append([action, rng.randrange(4),
                           [rng.choice((None, 0, 1, 2, 3)) for _ in range(rng.randrange(3))],
                           [rng.choice((None, 0, 1, 2, 3)) for _ in range(rng.randrange(3))]])
        elif action == "external":
            result.append([action, operations])
        elif action == "commit":
            result.append([action, rng.randrange(4), operations])
        else:
            result.append([action, rng.randrange(4)])
    return result


def sql_trace(trace):
    db = sqlite3.connect(":memory:")
    try:
        db.executescript("CREATE TABLE rows(position INTEGER PRIMARY KEY AUTOINCREMENT,value INTEGER NOT NULL);"
                         "CREATE TABLE state(revision INTEGER); INSERT INTO state VALUES(0);")
        captures, output = {}, []
        def view(scope):
            if scope is None:
                return [row[0] for row in db.execute("SELECT value FROM rows ORDER BY position")]
            return [row[0] for row in db.execute("SELECT value FROM rows WHERE value=? ORDER BY position", (scope,))]
        def apply(operations):
            with db:
                for kind, value in operations:
                    if kind == "add":
                        db.execute("INSERT INTO rows(value) VALUES(?)", (value,))
                    else:
                        db.execute("DELETE FROM rows WHERE position=(SELECT MIN(position) FROM rows WHERE value=?)", (value,))
                db.execute("UPDATE state SET revision=revision+1")
        for action in trace:
            kind, views = action[0], []
            if kind == "observe":
                scopes = [*action[3], *action[2]]
                captures[action[1]] = action[3], scopes, [view(scope) for scope in scopes]
                views, status = [view(scope) for scope in action[2]], "observed"
            elif kind == "external":
                apply(action[1]); status = "external"
            elif kind == "restart":
                captures.clear(); status = "restarted"
            elif kind == "close":
                status = "closed" if captures.pop(action[1], None) is not None else "retired"
            else:
                captured = captures.pop(action[1], None)
                if captured is None:
                    status = "retired"
                else:
                    writes, scopes, before = captured
                    if any(not any(scope is None or value == scope for scope in writes) for _, value in action[2]):
                        status = "scope"
                    elif before != [view(scope) for scope in scopes]:
                        status = "stale"
                    else:
                        apply(action[2]); status = "committed"
            output.append([status, db.execute("SELECT revision FROM state").fetchone()[0], view(None), views])
        return output
    finally:
        db.close()


def native_trace(trace, service, binary, worker):
    saved = sys.argv
    try:
        sys.argv = ["scope-fixture", str(service), str(binary), str(worker)]
        spec = importlib.util.spec_from_file_location("scope_fixture", ROOT / "tests/test_space_service.py")
        fixture = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = fixture
        spec.loader.exec_module(fixture)
    finally:
        sys.argv = saved
    E, S, Var = fixture.E, fixture.S, fixture.Var
    test = fixture.ServiceTests(); test.setUp()
    try:
        native = test.transaction_worker()
        space = test.client.open("scope-trace", "native-ordered")
        captures, output = {}, []
        def pattern(scope):
            return E("Value", Var(0) if scope is None else scope)
        for number, action in enumerate(trace):
            kind, views = action[0], []
            if kind == "observe":
                if action[1] in captures:
                    native.call(E("abort", test.fixture.socket, captures[action[1]]))
                reads = tuple(E("scope", str(index), pattern(scope)) for index, scope in enumerate(action[2]))
                writes = tuple(map(pattern, action[3]))
                value = native.call(E("scoped-observe", test.fixture.socket, space.describe(), reads, writes))
                if value[0] != S("SpaceScopedObserved"):
                    raise AssertionError((number, value))
                captures[action[1]], status = value, "observed"
                views = [[row[1] for row in item[2]] for item in value[5]]
            elif kind == "external":
                space.describe(); space.batch(*(E(op, E("Value", value)) for op, value in action[1]))
                status = "external"
            elif kind == "restart":
                test.client.close(); test.fixture.restart(crash=True)
                test.client = fixture.Client(test.fixture.socket)
                test.addCleanup(test.client.close)
                space = test.client.open("scope-trace", "native-ordered")
                status = "restarted"
            else:
                observation = captures[action[1]]
                if kind == "close":
                    value = native.call(E("abort", test.fixture.socket, observation))
                else:
                    operations = tuple(E(op, E("Value", row)) for op, row in action[2])
                    value = native.call(E("commit", test.fixture.socket, observation, f"scope-step-{number}", operations))
                if value[0] == S("reply") and value[2] == S("ok"):
                    status = "closed" if kind == "close" else "committed"
                elif value[0] == S("reply") and value[2] == S("error"):
                    status = {S("scope-conflict"): "stale", S("undeclared-write"): "scope",
                              S("expired-or-closed"): "retired", S("observation-identity"): "retired",
                              S("expired-service"): "retired"}.get(value[3])
                    if status is None:
                        raise AssertionError((number, value))
                else:
                    raise AssertionError((number, value))
            output.append([status, space.describe()[6], space.match(E("Value", Var(0)), Var(0)), views])
        return output
    finally:
        test.doCleanups()


def lean_trace(trace, project, output):
    def scope(value):
        return ".all" if value is None else f"(.only {value})"
    def operations(values):
        return "[" + ", ".join(f".{kind} {value}" for kind, value in values) + "]"
    terms = []
    for action in trace:
        if action[0] == "observe":
            terms.append(f".observe {action[1]} ["+", ".join(map(scope, action[2]))+"] ["+
                         ", ".join(map(scope, action[3]))+"]")
        elif action[0] == "external":
            terms.append(".external " + operations(action[1]))
        elif action[0] == "commit":
            terms.append(f".commit {action[1]} " + operations(action[2]))
        elif action[0] == "close":
            terms.append(f".close {action[1]}")
        else:
            terms.append(".restart")
    program = output / "NativeScopeTrace.lean"
    program.write_text("import Mettapedia.Machines.NativeStoreScopes\n"
                       "open Mettapedia.Machines.NativeStoreScopes\n"
                       "def main : IO Unit := do\n"
                       "  for line in scopedTraceLines {} ["+",\n    ".join(terms)+"] do\n"
                       "    IO.println line\n")
    result = subprocess.run(["lake", "env", "lean", "-j1", "--run", str(program)], cwd=project,
                            capture_output=True, text=True, timeout=120)
    if result.returncode:
        raise AssertionError(result.stdout+result.stderr)
    rows = []
    for line in result.stdout.splitlines():
        status, revision, values, views = line.split("|", 3)
        rows.append([status, int(revision), json.loads(values), json.loads(views)])
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("service", "cetta", "worker", "lean-project", "output"):
        parser.add_argument("--"+name, type=Path, required=True)
    parser.add_argument("--seed", type=int, default=20261026)
    args = parser.parse_args()
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=True)
    trace = commands(args.seed)
    expected = sql_trace(trace)
    actual = native_trace(trace, args.service.resolve(), args.cetta.resolve(), args.worker.resolve())
    model = lean_trace(trace, args.lean_project.resolve(), output)
    for number, (a, b, c) in enumerate(zip(expected, actual, model, strict=True)):
        if a != b or a != c:
            raise AssertionError({"step": number, "command": trace[number], "sqlite": a, "native": b, "lean": c})
    receipt = {"steps": len(trace), "commits": sum(row[0] == "committed" for row in actual),
               "seed": args.seed, "sqlite_version": sqlite3.sqlite_version,
               "library_sha256": digest(ROOT / "lib/petta/space_transaction.metta"),
               "service_sha256": digest(args.service), "worker_sha256": digest(args.worker),
               "model_sha256": digest(args.lean_project / "Mettapedia/Machines/NativeStoreScopes.lean"),
               "commands": trace, "observations": actual,
               "claim": "scoped trace agreement; not interpreter or filesystem verification"}
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2)+"\n")
    print(f"native scopes: {len(trace)} transitions agree in MeTTa, SQLite and Lean")


if __name__ == "__main__":
    main()
