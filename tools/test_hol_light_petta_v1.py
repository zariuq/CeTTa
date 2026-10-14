"""Run the authored HOL Light checker on an ordinary PeTTa installation."""

from __future__ import annotations

import argparse
import copy
import json
from pathlib import Path
import subprocess

from hol_light_petta_v1 import (LIBRARY, call, export_events, library_text,
                                profile_text, source_term, string, tagged)
from hol_light_proof_export_v1 import inspect_closure


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    repository = Path(__file__).resolve().parents[1]
    parser.add_argument("--petta-run", type=Path,
                        default=repository.parent / "PeTTa" / "run.sh")
    parser.add_argument("--workdir", type=Path,
                        default=repository / "tmp" / "hol-light-petta-tests")
    args = parser.parse_args()
    runner, work = args.petta_run.resolve(), args.workdir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    checks = 0

    def checked(condition: bool, label: str) -> None:
        nonlocal checks
        if not condition:
            raise AssertionError(label)
        checks += 1

    def execute(path: Path, label: str) -> str:
        result = subprocess.run(["bash", str(runner), str(path.resolve()), "--silent"],
                                cwd=runner.parent, text=True, capture_output=True)
        (work / (label + ".log")).write_text(result.stdout + result.stderr)
        checked(result.returncode == 0, label + " failed; inspect its retained log")
        return result.stdout

    fixtures = repository / "tests" / "hol_light"
    standard = json.loads((fixtures / "source" / "standard-theory.json").read_text())
    checked(profile_text(standard) == (repository / "lib" / "hol_light" / "profile.metta").read_text(),
            "profile is not the pinned generated source data")
    output = execute(fixtures / "kernel.metta", "kernel-controls")
    checked(output.count("✅") == 58 and "❌" not in output,
            "expected exactly 58 passing kernel controls")
    execute(fixtures / "definition-probe.metta", "separate-module-imports")

    example = repository / "examples" / "hol_light" / "kernel.metta"
    output = execute(example, "modular-example")
    observations = [line for line in output.splitlines() if line.startswith("(")]
    checked(len(observations) == 4, "example lost an observation")
    checked("(IdentityLaw true (SpecializedToBool true) (UsedAxioms HOL:Nil) (OrderedPremises 2))"
            in observations, "identity law or polymorphic instance failed")
    checked("(CaptureControl (FreeVariablePreserved true) (CapturedReadingAccepted false))"
            in observations, "capture control failed")
    checked("(FromHOLLight (Received true) (EqualityWithTruth true) (FalseClaimAccepted false)"
            " (UsedAxioms HOL:Nil) (Receipts 27))" in observations, "actual source proof failed")
    checked("(ForgedProofAccepted false)" in observations, "forged proof accepted")
    body = "\n".join(line for line in example.read_text().splitlines()
                     if not line.startswith("!(import! "))
    bundle = work / "standalone.metta"
    bundle.write_text(library_text() + "\n" + body + "\n")
    standalone = execute(bundle, "standalone-example")
    checked(standalone.splitlines() == observations,
            "standalone and separately imported modules disagree")

    nodes = [json.loads(line) for line in
             (fixtures / "source" / "primitives.nodes.jsonl").read_text().splitlines()]
    checked({"REFL", "TRANS", "MK_COMB", "ABS", "BETA", "ASSUME", "EQ_MP",
             "DEDUCT_ANTISYM_RULE", "INST", "INST_TYPE"}
            <= {node["rule"] for node in nodes}, "source panel omits a primitive rule")
    events, addresses = export_events(nodes, standard)
    checked(len(events) == len(addresses) == 190, "source accounting changed")

    def receive(label: str, selected: list[str], want: str) -> None:
        path = work / (label + ".metta")
        query = call("let", "$events", call("test:events"),
                     call("hol-light:accepted", call("hol-light:standard-receive", "$events")))
        path.write_text(library_text() + "\n" +
                        call("=", call("test:events"), call("quote", tagged(selected))) + "\n!" + query + "\n")
        checked(execute(path, label).strip() == want, label + " returned the wrong verdict")

    receive("source-all-primitives", events, "true")
    # HOL Light's primitive function operator is fun; -> is an available
    # user spelling. Both returned laws of this actual definition are checked.
    arrow_nodes = [json.loads(line) for line in
                   (fixtures / "source" / "arrow-type.nodes.jsonl").read_text().splitlines()]
    arrow_accounting = inspect_closure(fixtures / "source" / "arrow-type",
                                      ["ARROW_ABS_LAW", "ARROW_REP_LAW"])
    arrow_events, arrow_keys = export_events(arrow_nodes, standard)
    checked(arrow_accounting["nodes"] == len(arrow_keys) == 10 and len(arrow_events) == 9,
            "paired type-definition coverage changed")
    arrow_path = work / "source-arrow-type-definition.metta"
    roots = arrow_accounting["roots"]
    claims = {node["id"]: node["theorem"] for node in arrow_nodes}
    laws = [call("hol-light:check-result", "$result", arrow_keys[root["id"]], "HOL:Nil",
                 source_term(claims[root["id"]]["conclusion"])) for root in roots]
    query = call("let", "$events", call("test:events"),
                 call("let", "$result", call("hol-light:standard-receive", "$events"),
                      call("ArrowType", call("hol-light:accepted", "$result"), *laws,
                           call("hol-light:dependencies", "$result", arrow_keys[roots[1]["id"]]),
                           call("hol-light:receipt-count", "$result"))))
    arrow_path.write_text(library_text() + "\n" +
                         call("=", call("test:events"), call("quote", tagged(arrow_events))) + "\n!" + query + "\n")
    checked(execute(arrow_path, "source-arrow-type-definition").strip() ==
            "(ArrowType true true true HOL:Nil 19)",
            "source type definition lost a law, dependency account or receipt")
    def replace_operator(value: object) -> object:
        if isinstance(value, list):
            if len(value) == 3 and value[0] == "op" and value[1] == "->":
                return ["op", "fun", replace_operator(value[2])]
            return [replace_operator(item) for item in value]
        if isinstance(value, dict):
            return {key: replace_operator(item) for key, item in value.items()}
        return value
    reused_name = [replace_operator(node) for node in arrow_nodes]
    for node in reused_name:
        if node["rule"] == "TYPE_DEFINITION":
            node["arguments"][1] = "fun"
    receive("existing-function-type-name", export_events(reused_name, standard)[0], "false")
    changed = copy.deepcopy(nodes)
    changed[1]["theorem"]["conclusion"] = ["var", "false-claim", ["op", "bool", []]]
    receive("false-intermediate-claim", export_events(changed, standard)[0], "false")
    changed = copy.deepcopy(nodes)
    changed[-1]["arguments"][0] = changed[-1]["id"]
    try:
        export_events(changed, standard)
    except ValueError:
        checked(True, "forward premise")
    else:
        checked(False, "adapter accepted a forward premise")
    changed = copy.deepcopy(nodes)
    changed.append(changed[-1])
    try:
        export_events(changed, standard)
    except ValueError:
        checked(True, "duplicate source id")
    else:
        checked(False, "adapter accepted a duplicate proof id")

    # A modified T definition cannot borrow the pinned profile's proof by name.
    original = nodes[0]
    changed_body = ["const", "F", ["op", "bool", []]]
    mismatch = call("HOL:CheckedReuse", "mismatch",
                    call("DEFINITION", string("T"), "(Tyapp \"bool\" HOL:Nil)",
                         source_term(changed_body)), string("standard-definition:T"),
                    "HOL:Nil", source_term(original["theorem"]["conclusion"]))
    receive("mismatched-definition-body", [mismatch], "false")
    # An axiom label and an axiom formula must both match the selected profile.
    undeclared = call("HOL:CheckedStep", "invented",
                      call("AXIOM", string("ETA_AX"), "(Const \"T\" (Tyapp \"bool\" HOL:Nil))"),
                      "HOL:Nil", "(Const \"T\" (Tyapp \"bool\" HOL:Nil))")
    receive("mismatched-standard-axiom", [undeclared], "false")

    print(json.dumps({"harness_checks": checks, "kernel_controls": 58,
                      "exported_nodes_checked": len(nodes) + len(arrow_nodes), "library_components": len(LIBRARY),
                      "runtime": "ordinary PeTTa", "full_checker_soundness_proved": False}))


if __name__ == "__main__":
    main()
