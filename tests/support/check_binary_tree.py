#!/usr/bin/env python3
"""Check recursive packed records against an independent encoder."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import random
import subprocess

from check_binary_codec import ROOT, encode, observation, parse_forms, tensor


def packed(tree):
    if tree[0] == "leaf":
        return encode([b"", tree[1]])
    return encode([b"\1", tree[1], encode([packed(child) for child in tree[2]])])


def wire(tree):
    if tree[0] == "leaf":
        return f"(Binary:Leaf {tensor(tree[1])})"
    return f"(Binary:Node {tensor(tree[1])} (" + " ".join(map(wire, tree[2])) + "))"


def inspect(tree):
    if tree[0] == "leaf":
        return ["Binary:Leaf", observation(tree[1])]
    return ["Binary:Node", observation(tree[1]), [inspect(child) for child in tree[2]]]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cetta", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    trees = [("leaf", b""), ("leaf", bytes(range(256))), ("leaf", "λ●\0□".encode()),
             ("node", b"", []), ("node", b"Definition", [("leaf", b"x"), ("leaf", b"x")]),
             ("node", b"Use", [("leaf", b"\0"), ("node", b"Bindings", [])])]
    nested = ("leaf", b"!(side-effect)")
    for depth in range(12):
        nested = ("node", bytes([depth]), [nested])
        if depth in (0, 3, 7, 11):
            trees.append(nested)
    rng = random.Random(2702)

    def tree(depth):
        if depth == 0 or rng.randrange(3) == 0:
            return "leaf", rng.randbytes(rng.randrange(17))
        return "node", rng.randbytes(rng.randrange(4)), [tree(depth - 1) for _ in range(rng.randrange(4))]

    trees += [tree(4) for _ in range(20)]
    code = ['!(import! &self (library lib_binary_tree))', '''
(= (tree-view $tree)
   (case $tree
     (((Binary:Leaf $payload) (Binary:Leaf (tensor:inspect $payload)))
      ((Binary:Node $tag $children)
       (Binary:Node (tensor:inspect $tag) (map-atom $children $child (tree-view $child)))))))
(= (tree-test $tree)
   (case (binary-tree:encode $tree)
     (((Binary:TreeBytes $bytes)
       (case (binary-tree:decode $bytes)
         (((Binary:Tree $decoded)
           (TreeObservation (tensor:inspect $bytes) (tree-view $decoded) (binary-tree:byte-length $tree)))
          ($other $other))))
      ($other $other))))
''']
    expected = []
    for t in trees:
        index = len(expected)
        code.append(f"!(println! (TreeCase {index} (tree-test {wire(t)})))")
        expected.append(["TreeCase", str(index), ["TreeObservation", observation(packed(t)), inspect(t), str(len(packed(t)))]])
    controls = [
        (f"(binary-tree:decode {tensor(b'')})", ["Binary:TreeFault", "truncated"]),
        (f"(binary-tree:decode {tensor(encode([]))})", ["Binary:TreeFault", "InvalidRecord"]),
        (f"(binary-tree:decode {tensor(encode([b'\2', b'x']))})", ["Binary:TreeFault", "InvalidMarker"]),
        (f"(binary-tree:decode {tensor(encode([b'\1', b't', b'']))})", ["Binary:TreeFault", "truncated"]),
        (f"(binary-tree:decode {tensor(encode([b'\1', b't', encode([b''])]))})", ["Binary:TreeFault", "truncated"]),
        (f"(binary-tree:decode {tensor(packed(trees[0]) + b'\0')})", ["Binary:TreeFault", "trailing-data"]),
        (f"(binary-tree:decode-with-budget 0 {tensor(packed(trees[0]))})", ["Binary:TreeFault", "DecodeBudget"]),
        ("(binary-tree:encode (Binary:Leaf unknown))", ["Binary:TreeFault", "InvalidPayload"]),
        ("(binary-tree:encode (Binary:Node unknown ()))", ["Binary:TreeFault", "InvalidTag"]),
        (f"(binary-tree:encode (Binary:Node {tensor(b't')} unknown))", ["Binary:TreeFault", "InvalidChildren"]),
        ("(binary-tree:encode unknown)", ["Binary:TreeFault", "InvalidTree"]),
    ]
    for call, result in controls:
        index = len(expected)
        code.append(f"!(println! (TreeCase {index} {call}))")
        expected.append(["TreeCase", str(index), result])
    fixture = args.output / "trees.metta"
    fixture.write_text("\n".join(code) + "\n")
    result = subprocess.run([str(args.cetta.resolve()), "--lang", "petta", str(fixture.resolve())],
                            cwd=ROOT, capture_output=True, text=True, timeout=30)
    (args.output / "trees.log").write_text(result.stdout + result.stderr)
    actual = [f for f in parse_forms(result.stdout) if isinstance(f, list) and f and f[0] == "TreeCase"]
    wrong = [i for i, (a, b) in enumerate(zip(actual, expected)) if a != b]
    receipt = {"passed": result.returncode == 0 and len(actual) == len(expected) and not wrong,
               "checks": len(expected), "answers": len(actual), "mismatches": wrong,
               "scope": "complete encoded bytes, nesting, repeated children, packed payloads and malformed records"}
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt))
    if not receipt["passed"]:
        for i in wrong[:3]:
            print("expected", expected[i])
            print("actual  ", actual[i])
        print(result.stderr[-2000:])
        raise SystemExit(1)


if __name__ == "__main__":
    main()
