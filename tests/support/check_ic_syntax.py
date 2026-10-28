#!/usr/bin/env python3
"""Independent finite correspondence for exact ordered-tree IC factoring."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess

from check_incremental_compression import ROOT, data, feature, parse_forms, sexpr, unary


def size(tree):
    if tree[0] == "atom":
        return 1
    if tree[0] == "ref":
        return tree[1] + 1
    return 1 + size(tree[1]) + size(tree[2])


def occurs(name, tree):
    if tree[0] == "atom":
        return False
    if tree[0] == "ref":
        return tree[1] == name
    return occurs(name, tree[1]) or occurs(name, tree[2])


def abstract(name, body, tree):
    if tree == body:
        return ("ref", name)
    if tree[0] == "pair":
        return ("pair", abstract(name, body, tree[1]), abstract(name, body, tree[2]))
    return tree


def expand(name, body, tree):
    if tree[0] == "ref" and tree[1] == name:
        return body
    if tree[0] == "pair":
        return ("pair", expand(name, body, tree[1]), expand(name, body, tree[2]))
    return tree


def max_reference(tree):
    if tree[0] == "atom":
        return 0
    if tree[0] == "ref":
        return tree[1]
    return max(max_reference(tree[1]), max_reference(tree[2]))


def subtrees(tree):
    yield tree
    if tree[0] == "pair":
        yield from subtrees(tree[1])
        yield from subtrees(tree[2])


def wire(tree):
    if tree[0] == "atom":
        return ["IC:Leaf", tree[1]]
    if tree[0] == "ref":
        return ["IC:Ref", unary(tree[1])]
    return ["IC:Pair", wire(tree[1]), wire(tree[2])]


def factor_wire(feature):
    return ["IC:Factor", unary(feature[0]), wire(feature[1])]


def reference(source, fuel):
    residual, features = source, []
    stop = "IC:BudgetExhausted"
    for _ in range(fuel):
        fresh = max_reference(residual) + 1
        chosen = None
        for body in subtrees(residual):
            if occurs(fresh, residual):
                continue
            next_residual = abstract(fresh, body, residual)
            if expand(fresh, body, next_residual) != residual:
                continue
            if size(body) + fresh + 2 + size(next_residual) < size(residual):
                chosen = body, next_residual
                break
        if chosen is None:
            stop = "IC:NoCandidateAccepted"
            break
        body, residual = chosen
        features.append((fresh, body))
    restored = residual
    for name, body in reversed(features):
        restored = expand(name, body, restored)
    assert restored == source
    cost = sum(size(body) + name + 2 for name, body in features) + size(residual)
    assert cost <= size(source)
    return ["TreeObservation", [factor_wire(f) for f in features], wire(residual), stop,
            ["IC:Some", wire(restored)], ["IC:Some", str(cost)]]


def term_tree(term):
    """Ordered constructor AST: explicit list spine and a fixed terminator."""
    tail = ("atom", "SyntaxEnd")
    for item in reversed(term):
        value = term_tree(item) if isinstance(item, list) else ("atom", item)
        tail = ("pair", value, tail)
    return tail


def packet_tree(packet, atoms):
    """Injective proper-expression codec; empty lists have a reserved leaf."""
    if isinstance(packet, str):
        if packet not in atoms:
            atoms[packet] = "SyntaxAtom" + str(len(atoms) + 1)
        return ("atom", atoms[packet])
    tail = ("atom", "SyntaxNil")
    for item in reversed(packet):
        tail = ("pair", packet_tree(item, atoms), tail)
    return tail


def unpack_packet(tree, atoms):
    inverse = {name: token for token, name in atoms.items()}
    if tree[0] == "atom":
        return [] if tree[1] == "SyntaxNil" else inverse[tree[1]]
    values = []
    while tree[0] == "pair":
        values.append(unpack_packet(tree[1], atoms))
        tree = tree[2]
    if tree != ("atom", "SyntaxNil"):
        raise ValueError("not a proper expression spine")
    return values


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cetta", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--examples", type=Path)
    ap.add_argument("--module-core", type=Path)
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    a, b, c = ("atom", "A"), ("atom", "B"), ("atom", "C")
    branch = ("pair", a, ("pair", b, a))
    repeated = ("pair", ("pair", branch, branch), ("pair", branch, branch))
    sources = [a, b, ("ref", 0), ("ref", 2), ("pair", a, b), branch, repeated,
               ("pair", repeated, repeated), ("pair", ("ref", 1), repeated),
               ("pair", ("pair", branch, c), ("pair", branch, branch))]
    if args.examples:
        examples = json.loads(args.examples.read_text())
        # Include all generated scene sorts, not merely the easiest leaf forms.
        for category in ("Shape", "Colour", "Object", "Scene"):
            selected = [e for e in examples if e["sort"] == category]
            sources.extend(term_tree(e["term"]) for e in selected[-4:])
    packet_atoms = {}
    source_rule_count = 0
    bundle_cost = None
    if args.module_core:
        from check_feature_scene import canonical_list
        forms = parse_forms(args.module_core.read_text())
        cores = [f[1] for f in forms if isinstance(f, list) and f and f[0] == "SceneCore"]
        if len(cores) != 1 or cores[0][0] != "GSLTLanguageDefWireV1":
            raise ValueError("expected one assembled core")
        rules = canonical_list(cores[0][5])[:2]
        for rule in rules:
            tree = packet_tree(rule, packet_atoms)
            assert unpack_packet(tree, packet_atoms) == rule
            sources.append(tree)
        source_rule_count = len(rules)
        bundle = packet_tree(rules, packet_atoms)
        assert unpack_packet(bundle, packet_atoms) == rules
        sources.append(bundle)
        bundle_cost = {"source_units": size(bundle),
                       "compressed_units": int(reference(bundle, 4)[-1][1])}
        (args.output / "packet-atoms.json").write_text(json.dumps(packet_atoms, indent=2) + "\n")
    # Both families share one checker; importing them together must not create
    # duplicate answers or route a proposal through the other family's rules.
    code = ['!(import! &self (library lib_ic_syntax))',
            '!(import! &self (library lib_ic_repeated_block))', '''
(= (tree-test $fuel $source)
   (case (ic:compress-generated IC:Syntax $fuel $source)
     (((IC:Plan $features $residual $stop)
       (let* (($restored (ic:restore IC:Syntax $features $residual))
              ($cost (ic:plan-cost IC:Syntax $features $residual)))
         (TreeObservation $features $residual $stop $restored $cost)))
      ($other $other))))
''']
    expected = []
    savings = []
    for source in sources:
        for fuel in (0, 1, 4):
            index = len(expected)
            call = ["tree-test", unary(fuel), wire(source)]
            code.append(f"!(println! (TreeCase {index} {sexpr(call)}))")
            observation = reference(source, fuel)
            expected.append(["TreeCase", str(index), observation])
            savings.append(size(source) - int(observation[-1][1]))
    controls = [
        (["ic:extract", "IC:Syntax", factor_wire((1, branch)), wire(("pair", ("ref", 1), branch))], "IC:None"),
        (["ic:compress-generated", "IC:Syntax", unary(2), ["IC:Leaf", "1.0"]], ["IC:Invalid", "Source"]),
        (["ic:check", "IC:RepeatedBits", data([False, True] * 8), feature(2)],
         ["IC:Accepted", feature(2), data([False, True] * 4)]),
    ]
    for call, wanted in controls:
        index = len(expected)
        code.append(f"!(println! (TreeCase {index} {sexpr(call)}))")
        expected.append(["TreeCase", str(index), wanted])
    fixture = args.output / "syntax.metta"
    fixture.write_text("\n".join(code) + "\n")
    result = subprocess.run([str(args.cetta.resolve()), "--lang", "petta", str(fixture.resolve())],
                            cwd=ROOT, capture_output=True, text=True, timeout=30)
    (args.output / "syntax.log").write_text(result.stdout + result.stderr)
    actual = [form for form in parse_forms(result.stdout) if isinstance(form, list) and form and form[0] == "TreeCase"]
    wrong = [i for i, (a, b) in enumerate(zip(actual, expected)) if a != b]
    receipt = {"passed": result.returncode == 0 and len(actual) == len(expected) and not wrong,
               "checks": len(expected), "answers": len(actual), "mismatches": wrong,
               "strictly_smaller_cases": sum(s > 0 for s in savings),
               "max_saved_constructor_units": max(savings),
               "source_module_rules": source_rule_count,
               "module_bundle_cost": bundle_cost,
               "scope": "finite host correspondence; lossless factoring, not semantic rule invention"}
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt))
    if not receipt["passed"]:
        for i in wrong[:3]:
            print("expected", sexpr(expected[i]))
            print("actual  ", sexpr(actual[i]))
        print(result.stderr)
        raise SystemExit(1)


if __name__ == "__main__":
    main()
