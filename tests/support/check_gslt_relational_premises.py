"""Retain constrained Horn goals, ordered occurrences and open answer sharing."""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

import native_observation
from check_transform_swi_differential import read_forms, write_forms

ROOT = Path(__file__).resolve().parents[2]


def completed(command, **arguments):
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True,
                            timeout=90, **arguments)
    if result.returncode:
        raise RuntimeError(f"command exited {result.returncode}:\n{result.stdout}{result.stderr}")
    return result.stdout


def body(program, name):
    equations = [form for form in read_forms(program.read_text())
                 if isinstance(form, list) and len(form) == 3 and form[0] == "="
                 and isinstance(form[1], list) and form[1][:1] == [name]]
    if len(equations) != 1:
        raise AssertionError(f"expected one equation for {name}")
    return equations[0][2]


def heads(tree):
    found, pending = set(), [tree]
    while pending:
        node = pending.pop()
        if isinstance(node, list):
            if node and isinstance(node[0], str):
                found.add(node[0])
            pending.extend(node)
    return found


def qualify(args, work):
    source = ROOT / "tests/petta/gslt_relational_premises_v1.metta"
    capability = ROOT / "tests/petta/gslt_relational_capability_v1.metta"
    env = dict(os.environ, CETTA_PETTA_SEARCH_MACHINE="1")

    def compile_program(output, *additional, target="petta", closed=False):
        command = [str(args.compiler), "petta-direct", "--target", target,
                   "--source", str(source)]
        for item in additional:
            command += ["--source", str(item)]
        for entry in ("LessV1:00", "ColourV1:0", "ChooseV1:10"):
            command += ["--closed-entry-mode" if closed else "--entry-mode", entry]
        completed(command + ["--out", str(output)])

    program = work / "program.metta"
    compile_program(program)
    # Unknown external effects do not license the same call transformation.
    mixed = work / "mixed.metta"
    compile_program(mixed, capability)
    assert "gslt:LessV1" in heads(body(mixed, "gslt:CheckV1"))
    assert "gslt:mode:ThroughCapabilityV1:00" in heads(body(mixed, "gslt:IndirectCapabilityV1"))
    override = work / "override.metta"
    override.write_text("""(gslt-presentation-v1 NativeOverrideV1
      (signature (operator metta-equation 2) (operator harmless-v1 1)
        (operator loop-v1 1))
      (equations)
      (rewrites
        (rule harmless (head (metta-equation (harmless-v1 ?x) ?x)) (body))
        (rule recursive (head (metta-equation (loop-v1 ?x) (loop-v1 ?x))) (body))))
""")
    overridden = work / "overridden.metta"
    compile_program(overridden, override)
    assert "gslt:mode:LessV1:00" in heads(body(overridden, "gslt:CheckV1"))
    assert body(overridden, "loop-v1") == ["loop-v1", "$x"]
    he = work / "he.metta"
    compile_program(he, target="he")
    assert "gslt:mode:LessV1:00" in heads(body(he, "gslt:CheckV1"))

    queries = []
    test_count = 0

    def check(call, expected):
        nonlocal test_count
        queries.extend(["!", ["test", ["collapse", call], ["quote", expected]]])
        test_count += 1

    check(["gslt:CheckV1", ["CheckV1", "z", "z"]], [])
    check(["gslt:CheckV1", ["CheckV1", "z", ["s", "z"]]],
          [["CheckV1", "z", ["s", "z"]]])
    check(["gslt:CheckV1", ["CheckV1", ["s", "z"], ["s", ["s", "z"]]]],
          [["CheckV1", ["s", "z"], ["s", ["s", "z"]]]])
    check(["gslt:ChooseV1", ["ChooseV1", "z", "$colour"]],
          [["ChooseV1", "z", value] for value in ("red", "green", "green")])
    check(["gslt:entry:ColourV1:0"],
          [["gslt:result:ColourV1:0", value] for value in ("red", "green", "green")])
    check(["gslt:entry:ChooseV1:10", "z"],
          [["gslt:result:ChooseV1:10", value] for value in ("red", "green", "green")])
    payload = ["cons", "z", ["z"]]
    check(["gslt:JoinV1", ["JoinV1", payload, "$middle", "$output"]],
          [["JoinV1", payload, payload, payload]])
    check(["gslt:IndirectCapabilityV1", ["IndirectCapabilityV1", "z", "z"]],
          [["IndirectCapabilityV1", "z", "z"]])
    # The returned open value has one shared variable, not two copies.
    queries.extend(["!", ["collapse", ["gslt:SharedV1", ["SharedV1", "$first", "$second"]]]])
    driver = work / "driver.metta"
    driver.write_text(write_forms(queries))
    command = [sys.executable, str(ROOT / "tests/support/run_gslt_program.py"),
               str(args.runtime), "--lang", "petta", str(mixed), str(driver)]
    residual = work / "closed.metta"
    compile_program(residual, closed=True)
    closed_driver = work / "closed-driver.metta"
    closed_driver.write_text(write_forms([
        "!", ["test", ["collapse", ["gslt:entry:ChooseV1:10", "z"]],
               ["quote", [["gslt:result:ChooseV1:10", value]
                          for value in ("red", "green", "green")]]],
        "!", ["let", ["gslt:result:LessV1:00", "z", ["s", "$open"]],
              ["once", ["gslt:entry:LessV1:00"]],
              ["test", ["get-metatype", "$open"], "Variable"]]]))
    closed_command = command[:-2] + [str(residual), str(closed_driver)]
    def observe(definitions, queries, name, count):
        receipt = work / (name + ".events.jsonl")
        completed([str(args.runtime), "--lang", "petta", "--run-observation", str(receipt),
                   str(definitions), str(queries)], env=env)
        found = native_observation.read_observation(receipt)
        assert found["final"]["contract_exit"] == 0
        assert len(found["queries"]) == count
        return native_observation.project(found, [(i, i) for i in found["queries"]])

    frontiers = observe(mixed, driver, "mixed", test_count + 1)
    closed_frontiers = observe(residual, closed_driver, "closed", 2)
    # The PeTTa test service acknowledges success with its `true` symbol.
    acknowledgements = [frontier["answers"] for frontier in frontiers[:-1]]
    acknowledged = [(("symbol", "true".encode().hex()),)]
    assert all(answer == acknowledged for answer in acknowledgements), acknowledgements
    assert all(frontier["answers"] == acknowledged for frontier in closed_frontiers), closed_frontiers
    open_answer, = frontiers[-1]["answers"]
    variables = [token for token in open_answer if token[0] == "variable"]
    assert len(variables) == 2 and variables[0] == variables[1]

    # Earlier recursive work remains pending even before an empty later goal.
    pending_cases = [
        ("before-empty", program, "!(collapse (gslt:BeforeEmptyV1 (BeforeEmptyV1 z)))"),
        ("recursive-target-equation", overridden, "!(loop-v1 z)"),
    ]
    for name, definitions, query in pending_cases:
        query_path = work / (name + ".metta")
        query_path.write_text(query + "\n")
        native_observation.check_petta_fuel_exhaustion(
            args.runtime, [definitions, query_path], work / (name + ".events.jsonl"), env=env)
    # The production corpus records only the qualified original invocation,
    # once all local checks have passed; administrative probes stay private.
    if env.get("CETTA_GSLT_RECORD_DIR"):
        completed(command, env=env)
        completed(closed_command, env=env)
    print("PASS: compiled relational premises on CeTTa: ordered duplicates, open sharing, "
          "conservative effects and pending work")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("compiler", type=Path)
    parser.add_argument("runtime", type=Path)
    parser.add_argument("--work", required=True, type=Path)
    args = parser.parse_args()
    args.compiler, args.runtime = args.compiler.resolve(), args.runtime.resolve()
    parent = args.work.resolve()
    parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="case-", dir=parent) as directory:
        qualify(args, Path(directory))


if __name__ == "__main__":
    main()
