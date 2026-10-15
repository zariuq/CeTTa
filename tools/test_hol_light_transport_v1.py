"""Transport received HOL Light proofs into Prime theory spaces and check them.

The pinned HOL Light exports are received by the hosted kernel one event per
query, then every proof node is replayed into a target theory space, one node
per query. Each replayed step is a theorem the target's own checker verifies.
The check requires that every node is transported, that the root's target
statement is the expected translation, and that the target's commitments are
exactly the expected ones.  The worked example, with its negative controls,
is run against its expected output as well.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess

from hol_light_petta_v1 import export_events

LIBRARY = ("data", "syntax", "kernel", "definitions", "derived", "profile", "receive", "transport")

# Each target: its namespace, its selection, a declaration it needs, and how
# HOL Light's ind is read there.
TARGETS = {
    "logic": ("hol", "(hol:profile base)", "(: hl-ind (u 0))", "(HOL:Interpretation hl-ind)"),
    "sets": ("set", "(set:profile megalodon-hotg)", None, "(HOL:Interpretation set)"),
}

# Expected root statements and commitments, per export and target.
EXPECTED = {
    "equality_truth": {
        "root": 17,
        "logic": ("(all prop (lam (x0 : prop) (eq prop x0 (eq prop x0 hl:T))))", "(hl:propext)"),
        "sets": ("(all prop (lam (x0 : prop) (eq prop x0 (eq prop x0 hl:T))))", "(hl:propext)"),
    },
}


def program(repository: Path, nodes: list[dict], standard: dict, target: str,
            root_id: int | None = None) -> str:
    namespace, profile, declaration, interpretation = TARGETS[target]
    events, keys = export_events(nodes, standard)
    lines = ["(scope:profile lexical-fresh per-call reference)"]
    library = repository / "lib" / "prime" / "hol_light"
    lines += [f"!(import! &self {library / name}.metta)" for name in LIBRARY]
    setup = f"(add-atom $s {declaration})" if declaration else "()"
    lines.append(f"!(bind! &target (let $s (new-space) (let $p (add-atom $s {profile}) "
                 f"(let $r (add-atom $s ({namespace}:rule hol-equality)) (let $t {setup} $s)))))")
    lines.append("!(bind! &received (case (hol-light:standard-receive HOL:Nil) "
                 "(((Some $s) $s) ($r (HOL:Refused $r)))))")
    for event in events:
        lines.append("!(bind! &received (let $state &received (case (hol-light:receive-from $state "
                     f"(function (return (HOL:Cons {event} HOL:Nil)))) (((Some $s) $s) "
                     "($r (HOL:Refused $r))))))")
    lines.append("!(let $state &received (case $state (((HOL:State $o $c $a $cache $r) "
                 "(Received (hol-light:length $cache))) ($x (NotReceived $x)))))")
    for node in nodes:
        lines.append(f"!(let $state &received (let $name (hol-light:transport-replay &target "
                     f"{interpretation} $state {keys[node['id']]} HOL:Nil) (Step {node['id']} $name)))")
    root = keys[nodes[-1]["id"]] if root_id is None else keys[root_id]
    lines.append(f"!(let $state &received (let $name (hol-light:transport-replay &target "
                 f"{interpretation} $state {root} HOL:Nil) (Root ({namespace}:known-proposition &target $name))))")
    lines.append("!(Commitments (hol-light:transport-commitments &target))")
    return "\n".join(lines) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    repository = Path(__file__).resolve().parents[1]
    parser.add_argument("--binary", type=Path, default=repository / "cetta")
    parser.add_argument("--workdir", type=Path,
                        default=repository / "runtime" / "hol-light-transport")
    parser.add_argument("--timeout", type=int, default=1800)
    args = parser.parse_args()
    args.binary = args.binary.resolve()
    work = args.workdir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    source = repository / "tests" / "hol_light" / "source"
    standard = json.loads((source / "standard-theory.json").read_text())
    checks = 0
    for export, expected in EXPECTED.items():
        nodes = [json.loads(line) for line in (source / f"{export}.nodes.jsonl").read_text().splitlines()]
        for target in TARGETS:
            text = program(repository, nodes, standard, target, expected["root"])
            path = work / f"{export}-{target}.metta"
            path.write_text(text)
            result = subprocess.run([str(args.binary), "--lang", "prime", str(path)],
                                    capture_output=True, text=True, timeout=args.timeout)
            (work / f"{export}-{target}.out").write_text(result.stdout + result.stderr)
            if result.returncode != 0:
                raise AssertionError(f"{export} into {target}: exit {result.returncode}")
            lines = result.stdout.splitlines()
            steps = [line for line in lines if line.startswith("[(Step ")]
            if len(steps) != len(nodes):
                raise AssertionError(f"{export} into {target}: {len(steps)} of {len(nodes)} nodes answered")
            refused = [line for line in steps if "TransportRefused" in line or "TransportUnsupported" in line]
            if refused:
                raise AssertionError(f"{export} into {target}: refused {refused[0][:300]}")
            statement, commitments = expected[target]
            if f"[(Root {statement})]" not in lines:
                root = [line for line in lines if line.startswith("[(Root ")]
                raise AssertionError(f"{export} into {target}: root statement {root}")
            if f"[(Commitments {commitments})]" not in lines:
                raise AssertionError(f"{export} into {target}: commitments differ")
            checks += 1
            print(f"PASS: {export} into {target}: {len(nodes)} steps transported and checked")
    example = repository / "examples" / "prime" / "hol_light_transport.metta"
    result = subprocess.run([str(args.binary), "--lang", "prime", str(example)],
                            capture_output=True, text=True, timeout=args.timeout)
    if result.returncode != 0 or result.stdout.rstrip("\n") != \
            example.with_suffix(".expected").read_text().rstrip("\n"):
        (work / "example.out").write_text(result.stdout + result.stderr)
        raise AssertionError(f"worked example differs from its expected output (exit {result.returncode})")
    checks += 1
    print("PASS: worked example with its negative controls")
    print(f"HOL Light transport: {checks} checks passed")


if __name__ == "__main__":
    main()
