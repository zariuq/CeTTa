"""Check initial registration, incremental fallback and held call values."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

import check_gslt_through_machine as bridge
import native_observation as observation

ROOT = Path(__file__).resolve().parents[2]
CASES = [
    ("forward", "(= (f $x) (g $x))\n(= (g $x) (Box $x))", "!(f 7)"),
    ("duplicates", "(= (pick) 1)\n(= (pick) 2)\n(= (pick) 2)", "!(collapse (pick))"),
    ("scopes", "(= (f $x) (Pair $x $x))\n(= (g $x) (Pair $x $y))",
     "!(let $a (f one) (let $b (g two) (Both $a $b)))"),
    ("effects", "(= (effect $x) (let $_ (println! $x) $x))\n(= (go) (let $_ (effect first) (effect second)))", "!(go)"),
    ("free-data", "(= (held) (quote (cons a (b c))))\n(= (choose (quote (cons a (b c)))) yes)",
     "!(held)\n!(choose (quote (cons a (b c))))"),
    ("private-helpers", "(= (owned) (Pair a b))",
     "!(collapse (match &self (= (pm:initial-declare-text $x) $y) leaked))\n"
     "!(collapse (match &self (= (pm:initial-define-text $x) $y) leaked))\n"
     "!(collapse (match &self (= (owned) $x) $x))"),
    ("open-tail", "(= (split (cons-atom $h $t)) (Pair $h $t))",
     "!(collapse (split (quote (a b c))))\n!(collapse (split ()))\n!(collapse (split 9))"),
    ("repeated", "(= (repeat (cons-atom $x (cons-atom $x ()))) $x)",
     '!(collapse (repeat (quote (a a))))\n!(collapse (repeat (quote (a b))))\n'
     '!(collapse (repeat (quote ("a" a))))'),
    ("nested-kinds", "(= (shape (cons-atom (cons-atom $tag ()) (cons-atom $value ()))) (Pair $tag $value))",
     '!(shape (quote ((Nullary) 3)))\n!(shape (quote ((Nullary) "3")))\n'
     '!(collapse (shape (quote (Nullary 3))))'),
    ("literal-operation", "(= (shape (quote (cons-atom a (b)))) yes)",
     "!(collapse (shape (quote (cons-atom a (b)))))\n!(collapse (shape (quote (a b))))"),
    ("head-view", "(= (front (cons-atom $h $t)) $h)",
     "!(front (quote (a b)))\n!(front (quote (cons x (y))))"),
    ("computed-leaf", "(= (plusone $x) (+ $x 1))\n(= (pick $x) (let (cons-atom (plusone 1) ()) $x yes))",
     "!(collapse (pick (quote (2))))\n!(collapse (pick (quote (3))))"),
    ("quotation", "(= (held $x) (let (quote (cons-atom $a $b)) $x (Pair $a $b)))",
     "!(held (quote (cons-atom one two)))\n!(collapse (held (quote (one two))))"),
    ("call-values", "", "\n".join([
        "!(call (+ 1 2))", "!(call (quote (+ 1 2)))",
        "!(let $v (quote (+ 1 2)) (call $v))", "!(let $v (quote (+ 1 2)) (reduce $v))",
        "!(let $v (quote (+ 1 2)) (eval $v))", "!(let $v 7 (call $v))",
        "!(let $v atom (call $v))", "!(collapse (let $v 7 (reduce $v)))",
        "!(let $v (quote (Unknown 4)) (call $v))",
        "!(let $v (quote (Unknown 4)) (reduce $v))",
        '!(let $v "text" (call $v))', '!(collapse (let $v "text" (reduce $v)))',
        "!(let $v (quote ()) (call $v))", "!(collapse (let $v (quote ()) (reduce $v)))",
        "!(let $v (quote (quote a)) (call $v))", "!(let $v (quote (quote a)) (reduce $v))"])),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--work", required=True, type=Path)
    args = parser.parse_args()
    runtime, work = args.binary.resolve(), args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    bridge.corpus.FIRE, bridge.corpus.TURNS = "transform", (1000, 20)
    results = []
    for name, program, queries in CASES:
        case = work / name
        case.mkdir(exist_ok=True)
        inputs = [case / "program.metta", case / "queries.metta"]
        for path, text in zip(inputs, (program, queries)):
            path.write_text(text + "\n")

        def run(paths, label, record=False):
            receipt = case / (label + ".events.jsonl")
            command = [str(runtime), "--run-observation", str(receipt)]
            directory = os.environ.get("CETTA_GSLT_RECORD_DIR")
            if record and directory:
                command = [sys.executable, str(ROOT / "tests/support/gslt_invocation.py"),
                           "record", directory, "3", *command]
            command += ["--lang", "petta", *(str(path) for path in paths)]
            result = subprocess.run(command, cwd=ROOT, capture_output=True, timeout=90)
            (case / (label + ".out")).write_bytes(result.stdout)
            (case / (label + ".err")).write_bytes(result.stderr)
            assert result.returncode == 0, (name, label, result.stderr[-1500:])
            found = observation.read_observation(receipt)
            assert found["final"]["contract_exit"] == 0, (name, label)
            return found

        native = run(inputs, "native", record=True)
        plan = dict(queries=0, guests=[])
        machine_paths = []
        for index, source in enumerate((program, queries)):
            path = case / (str(index) + ".machine.metta")
            path.write_text(bridge.through_the_machine(source, bridge.swi_lane.base.LIBRARY_IMPORT,
                                                     initialize=index == 0, plan=plan))
            machine_paths.append(path)
        machine = run(machine_paths, "machine")
        for number in plan.get("registration_preconditions", []):
            assert machine["queries"][number]["answers"] == [[["symbol", "true".encode().hex()]]], name
        for span in plan["guests"]:
            assert machine["queries"][span["status"]]["answers"] == [[["symbol", "pm:halted".encode().hex()]]], name
        assert observation.project(native, [(i, i) for i in native["queries"]]) == observation.project(machine, plan["guests"]), name
        results.append(name)

    for source, initialize in [("(= (f) 1)\n!(f)\n(= (f) 2)", True), ("(Row a)", True),
                               ("!(import! &self user_module)", True), ("(: atom Data)", True),
                               ("", True), ("(= (later) 1)", False)]:
        plan = dict(queries=0, guests=[])
        bridge.through_the_machine(source, bridge.swi_lane.base.LIBRARY_IMPORT, initialize=initialize, plan=plan)
        assert not plan.get("registration_preconditions"), source
    (work / "qualified.json").write_text(json.dumps(dict(programs=results, incremental_fallbacks=6), indent=2) + "\n")
    print(f"PASS: {len(results)} complete structural/effect comparisons and six incremental registration fallbacks")


if __name__ == "__main__":
    main()
