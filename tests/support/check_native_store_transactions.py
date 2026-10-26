#!/usr/bin/env python3
"""Replay one ordered transaction trace through MeTTa, SQLite and Lean.

SQLite is an isolated in-memory oracle. Native operations use the real space
service and authored space:observe/space:commit helpers. Lean runs the public
NativeStoreTransactions definitions, not a second Python model.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import random
import sqlite3
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))


def commands(seed):
    result = [
        ["observe", 0, None],
        ["external", [["add", 7], ["add", 7]]],
        ["commit", 0, [["add", 9]]],
        ["observe", 1, 7],
        ["commit", 1, [["add", 7], ["add", 8]]],
        ["commit", 1, [["remove", 7]]],
        ["commit", 1, [["add", 7]]],
        ["observe", 2, None],
        ["commit", 2, []],
        ["commit", 2, []],
    ]
    rng = random.Random(seed)
    for _ in range(150):
        action = rng.choice(("observe", "external", "commit", "commit"))
        ops = [[rng.choice(("add", "remove")), rng.randrange(5)] for _ in range(rng.randrange(4))]
        if action == "observe":
            result.append([action, rng.randrange(3), rng.choice((None, 0, 1, 2))])
        elif action == "external":
            result.append([action, ops])
        else:
            result.append([action, rng.randrange(3), ops])
    return result


def sql_trace(trace):
    db = sqlite3.connect(":memory:")
    try:
        db.executescript("CREATE TABLE rows(position INTEGER PRIMARY KEY AUTOINCREMENT,value INTEGER NOT NULL);"
                         "CREATE TABLE state(revision INTEGER); INSERT INTO state VALUES(0);")
        observed, output = {}, []
        def revision():
            return db.execute("SELECT revision FROM state").fetchone()[0]
        def apply(ops):
            with db:
                for kind, value in ops:
                    if kind == "add":
                        db.execute("INSERT INTO rows(value) VALUES(?)", (value,))
                    else:
                        db.execute("DELETE FROM rows WHERE position=(SELECT MIN(position) FROM rows WHERE value=?)", (value,))
                db.execute("UPDATE state SET revision=revision+1")
        for action in trace:
            kind = action[0]
            if kind == "observe":
                observed[action[1]] = revision(), action[2]
                status = "observed"
            elif kind == "external":
                apply(action[1]); status = "external"
            else:
                before, scope = observed[action[1]]
                if scope is not None and any(value != scope for _, value in action[2]):
                    status = "scope"
                elif before != revision():
                    status = "stale"
                else:
                    apply(action[2]); status = "committed"
            rows = [row[0] for row in db.execute("SELECT value FROM rows ORDER BY position")]
            output.append([status, revision(), rows])
        return output
    finally:
        db.close()


def native_trace(trace, service, binary, worker):
    # Reuse the existing process-owning fixture; its cleanup stops every worker
    # and service even when an assertion or transport operation fails.
    saved = sys.argv
    try:
        sys.argv = ["space-fixture", str(service), str(binary), str(worker)]
        spec = importlib.util.spec_from_file_location("space_transaction_fixture", ROOT / "tests/test_space_service.py")
        fixture = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = fixture
        spec.loader.exec_module(fixture)
    finally:
        sys.argv = saved
    E, S, Var = fixture.E, fixture.S, fixture.Var
    test = fixture.ServiceTests()
    test.setUp()
    try:
        native = test.transaction_worker()
        space = test.client.open("trace-ordered", "native-ordered")
        observed, output = {}, []
        read = (E("scope", "rows", E("Value", Var(0)), Var(0)),)
        for number, action in enumerate(trace):
            kind = action[0]
            if kind == "observe":
                writes = (E("Value", Var(0) if action[2] is None else action[2]),)
                value = native.call(E("observe", test.fixture.socket, space.describe(), read, writes, 4096))
                if value[0] != S("SpaceObserved"):
                    raise AssertionError((number, value))
                observed[action[1]] = value
                status = "observed"
            elif kind == "external":
                space.describe()
                space.batch(*(E(op, E("Value", row)) for op, row in action[1]))
                status = "external"
            else:
                operations = tuple(E(op, E("Value", row)) for op, row in action[2])
                value = native.call(E("commit", test.fixture.socket, observed[action[1]],
                                      f"trace-{number}", operations))
                if value == E("SpaceRefused", S("undeclared-write")):
                    status = "scope"
                elif value[0] == S("reply") and value[2] == S("ok"):
                    status = "committed"
                elif value[0] == S("reply") and value[2:] == (S("error"), S("stale-revision")):
                    status = "stale"
                else:
                    raise AssertionError((number, value))
            output.append([status, space.describe()[6], space.match(E("Value", Var(0)), Var(0))])
        return output
    finally:
        test.doCleanups()


def lean_trace(trace, project, output):
    def ops(values):
        return "[" + ", ".join(f".{kind} {value}" for kind, value in values) + "]"
    terms = []
    for action in trace:
        if action[0] == "observe":
            scope = ".all" if action[2] is None else f"(.only {action[2]})"
            terms.append(f".observe {action[1]} {scope}")
        elif action[0] == "external":
            terms.append(".external " + ops(action[1]))
        else:
            terms.append(f".commit {action[1]} " + ops(action[2]))
    program = output / "NativeStoreTrace.lean"
    program.write_text("import Mettapedia.Machines.NativeStoreTransactions\n"
                       "open Mettapedia.Machines.NativeStoreTransactions\n"
                       "def main : IO Unit := do\n"
                       "  for line in traceLines {} [" + ",\n    ".join(terms) + "] do\n"
                       "    IO.println line\n")
    result = subprocess.run(["lake", "env", "lean", "--run", str(program)], cwd=project,
                            capture_output=True, text=True, timeout=120)
    if result.returncode:
        raise AssertionError(result.stdout + result.stderr)
    rows = []
    for line in result.stdout.splitlines():
        status, revision, values = line.split("|", 2)
        rows.append([status, int(revision), json.loads(values)])
    return rows


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--service", type=Path, required=True)
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--worker", type=Path, required=True)
    parser.add_argument("--lean-project", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seed", type=int, default=20261026)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    trace = commands(args.seed)
    expected = sql_trace(trace)
    actual = native_trace(trace, args.service.resolve(), args.cetta.resolve(), args.worker.resolve())
    model = lean_trace(trace, args.lean_project.resolve(), output)
    for number, (a, b, c) in enumerate(zip(expected, actual, model, strict=True)):
        if a != b or a != c:
            raise AssertionError({"step": number, "command": trace[number], "sqlite": a, "native": b, "lean": c})
    receipt = {"steps": len(trace), "seed": args.seed, "sqlite_version": sqlite3.sqlite_version,
               "native_library_sha256": digest(ROOT / "lib/space_service.metta"),
               "service_sha256": digest(args.service), "worker_sha256": digest(args.worker),
               "model_sha256": digest(args.lean_project / "Mettapedia/Machines/NativeStoreTransactions.lean"),
               "commands": trace, "observations": actual,
               "claim": "trace agreement and model laws; not interpreter or filesystem verification"}
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(f"native transactions: {len(trace)} transitions agree in MeTTa, SQLite and Lean")


if __name__ == "__main__":
    main()
