#!/usr/bin/env python3
"""Exact compact forests, unique-node folds and owned-resource controls."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import random
import subprocess

from check_binary_codec import ROOT, observation, parse_forms, tensor
from check_binary_tree import inspect, packed, wire
from check_ic_archive import uint
from check_ic_search import check_controls


def compact(forest):
    records, interned = [], {}

    def put(tree):
        children = tuple(put(c) for c in tree[2]) if tree[0] == "node" else ()
        key = tree[0], tree[1], children
        if key not in interned:
            interned[key] = len(records)
            records.append(key)
        return interned[key]

    roots = [put(tree) for tree in forest]
    return roots, records


def records_wire(roots, records):
    rows = [f'(Binary:Leaf {tensor(payload)})' if kind == "leaf" else
            f'(Binary:Node {tensor(payload)} ({" ".join(map(str, children))}))'
            for kind, payload, children in records]
    return f'(Dag:Forest ({" ".join(map(str, roots))}) ({" ".join(rows)}))'


def record_view(record):
    kind, payload, children = record
    return ["Binary:Leaf", observation(payload)] if kind == "leaf" else \
        ["Binary:Node", observation(payload), list(map(str, children))]


def fold(records):
    values = []
    for kind, payload, children in records:
        values.append(len(payload) + (sum(values[i] for i in children) if kind == "node" else 1))
    return values


def encoded(roots, records, version=b"DAG1"):
    def vector(items):
        return "node", b"", items
    def numbers(items):
        return vector([("leaf", uint(n)) for n in items])
    rows = [("node", b"\3", [("leaf", payload)]) if kind == "leaf" else
            ("node", b"\4", [("leaf", payload), numbers(children)])
            for kind, payload, children in records]
    return packed(("node", b"\1", [("leaf", version),
                  ("node", b"\2", [numbers(roots), vector(rows)])]))


def measure(lengths):
    head = len(lengths).bit_length()
    bits = head + 2 * head.bit_length() + 1
    bits += sum(8 * n + 2 * (8 * n).bit_length() + 1 for n in lengths)
    return (bits + 7) // 8


def measured(records):
    values = []
    for kind, payload, children in records:
        values.append(measure([0, len(payload)]) if kind == "leaf" else
                      measure([1, len(payload), measure([values[i] for i in children])]))
    return values


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cetta", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    check_controls((ROOT / "lib/dag.metta").read_text())
    code = ['!(println! (DagUnimported (dag:from-forest ())))',
            '!(import! &self (library lib_dag_codec))', '''
(= (dag-record-view $record)
   (case $record
     (((Binary:Leaf $bytes) (Binary:Leaf (tensor:inspect $bytes)))
      ((Binary:Node $bytes $children) (Binary:Node (tensor:inspect $bytes) $children)))))
(= (dag-view $handle)
   (case (dag:export $handle)
     (((Dag:Forest $roots $records)
       (Dag:Forest $roots (map-atom $records $record (dag-record-view $record))))
      ($other $other))))
(= (dag-tree-view $tree)
   (case $tree
     (((Binary:Leaf $bytes) (Binary:Leaf (tensor:inspect $bytes)))
      ((Binary:Node $bytes $children)
       (Binary:Node (tensor:inspect $bytes) (map-atom $children $child (dag-tree-view $child)))))))
(= (dag-leaf-size $bytes) (+ 1 (index-atom (tensor:shape $bytes) 0)))
(= (dag-node-size $bytes $children)
   (+ (index-atom (tensor:shape $bytes) 0) (foldl-atom $children 0 $a $b (+ $a $b))))
(= (dag-effect-leaf $bytes) (let $effect (println! "DAG-UNIQUE-VISIT") 1))
(= (dag-effect-node $bytes $children)
   (let $effect (println! "DAG-UNIQUE-VISIT") (+ 1 (foldl-atom $children 0 $a $b (+ $a $b)))))
(= (dag-test $source)
   (let* (($handle (dag:from-forest $source)) ($wire (dag-view $handle))
          ($values (dag:fold-unique $handle dag-leaf-size dag-node-size))
          ($lengths (dag:encoded-lengths $handle))
          ($trees (map-atom (dag:roots $handle) $root (dag-tree-view (dag:unfold $handle $root))))
          ($closed (dag:close $handle)))
     (DagObservation $wire $values $trees $lengths)))
''']
    wanted = []

    def add(call, expected):
        i = len(wanted)
        code.append(f'!(println! (DagCase {i} {call}))')
        wanted.append(["DagCase", str(i), expected])

    leaf = ("leaf", b"x\0\xff")
    repeated = ("node", b"tuple", [leaf, leaf, ("node", b"", []), leaf])
    forests = [[], [leaf], [leaf, leaf], [("node", leaf[1], [])], [repeated, repeated, leaf],
               [("leaf", b"+0"), ("leaf", b"-0"), ("leaf", b"1"), ("leaf", b"1.0")],
               [("leaf", b"prefix\0left"), ("leaf", b"prefix\0right"), ("leaf", b"prefix")],
               [("leaf", bytes([94, 86, 34, 32, 0, 0, 0, 0])),
                ("leaf", bytes([39, 77, 39, 32, 0, 0, 0, 0])),
                ("leaf", bytes([94, 86, 34, 32, 0, 0, 0, 0]))]]
    rng = random.Random(2710)

    def tree(depth):
        if not depth or rng.randrange(3) == 0:
            return "leaf", rng.choice([b"", b"same", b"\0", b"\xff", "λ".encode()])
        child = tree(depth - 1)
        return "node", rng.randbytes(rng.randrange(4)), [child] * rng.randrange(4)

    forests += [[tree(4), tree(3)] for _ in range(10)]
    for forest in forests:
        roots, records = compact(forest)
        values = fold(records)
        add('(dag-test (' + " ".join(map(wire, forest)) + '))',
            ["DagObservation", ["Dag:Forest", list(map(str, roots)), list(map(record_view, records))],
             [str(values[root]) for root in roots], list(map(inspect, forest)),
             [str(len(packed(tree))) for tree in forest]])
        add(f'(let $h (dag:from-records {records_wire(roots, records)}) '
            '(let $v (dag-view $h) (let $closed (dag:close $h) $v)))',
            ["Dag:Forest", list(map(str, roots)), list(map(record_view, records))])

    for payload, data in [('(tensor:from-coo u8 (2) ((0 0) (1 255)))', bytes([0, 255])),
                          ('(tensor:view (tensor:from-values u8 (3) (10 20 30)) (3) (-1) 2)', bytes([30, 20, 10]))]:
        tree = ("leaf", data)
        add(f'(dag-test ((Binary:Leaf {payload})))',
            ["DagObservation", ["Dag:Forest", ["0"], [["Binary:Leaf", observation(data)]]],
             [str(len(data) + 1)], [inspect(tree)], [str(len(packed(tree)))]])
    add('(dag:from-forest ((Binary:Leaf (tensor:from-coo u8 (2) ((0 0))))))',
        ["Dag:Fault", "InvalidPayload"])

    for forest in [forests[i] for i in (0, 1, 4, 7)]:
        roots, records = compact(forest)
        add(f'''(let $original (dag:from-records {records_wire(roots, records)})
          (case (dag:pack $original)
            (((Binary:TreeBytes $bytes)
              (let* (($closed (dag:close $original))
                     ($restored (dag:unpack $bytes))
                     ($view (dag-view $restored))
                     ($released (dag:close $restored)))
                (DagTransport (tensor:inspect $bytes) $view))))))''',
            ["DagTransport", observation(encoded(roots, records)),
             ["Dag:Forest", list(map(str, roots)), list(map(record_view, records))]])
    add(f'(dag:unpack {tensor(encoded([], [], b"BAD1"))})', ["Dag:Fault", "WrongVersion"])
    add(f'(dag:unpack {tensor(encoded([0], [("node", b"", (0,))]))})', ["Dag:Fault", "InvalidReference"])

    empty = tensor(b"")
    for bad, reason in [
        (f'(Dag:Forest (0) ((Binary:Node {empty} (0))))', "InvalidReference"),
        (f'(Dag:Forest (0) ((Binary:Node {empty} (1)) (Binary:Leaf {empty})))', "InvalidReference"),
        (f'(Dag:Forest (0) ((Binary:Node {empty} (-1))))', "InvalidReference"),
        (f'(Dag:Forest (0) ((Binary:Leaf {empty}) (Binary:Node {empty} (0.0))))', "InvalidReference"),
        (f'(Dag:Forest (2) ((Binary:Leaf {empty})))', "InvalidRoot"),
        (f'(Dag:Forest (0.0) ((Binary:Leaf {empty})))', "InvalidRoot"),
        ('(Dag:Forest (0) ((Binary:Leaf "not packed")))', "InvalidPayload"),
        (f'(Dag:Forest (0) ((Binary:Leaf {empty} extra)))', "InvalidTree"),
        ('(Dag:Forest)', "InvalidRecords"),
    ]:
        add(f'(dag:from-records {bad})', ["Dag:Fault", reason])
    # Duplicate wire entries canonicalize without changing the selected values.
    add(f'''(let $h (dag:from-records (Dag:Forest (2 1 0)
         ((Binary:Leaf {empty}) (Binary:Leaf {empty}) (Binary:Node {empty} (0 1)))))
       (let $v (dag-view $h) (let $closed (dag:close $h) $v)))''',
        ["Dag:Forest", ["1", "0", "0"], [["Binary:Leaf", observation(b"")],
         ["Binary:Node", observation(b""), ["0", "0"]]]])
    add(f'''(let $h (dag:from-forest ((Binary:Leaf {empty})))
       (let (NativeHandle $kind $id) $h
         (let $forged (dag:roots (NativeHandle $kind (+ $id 0)))
           (let $real (dag:roots $h) (let $closed (dag:close $h) (Ownership $forged $real))))))''',
        ["Ownership", ["Dag:Fault", "Released"], ["0"]])
    add(f'''(let $h (dag:from-forest ((Binary:Leaf {empty})))
       (let $closed (dag:close $h) (dag:roots $h)))''', ["Dag:Fault", "Released"])
    add(f'''(let $h (dag:from-forest ((Binary:Leaf {empty})))
       (let $result (dag:node $h 1) (let $closed (dag:close $h) $result)))''', ["Dag:Fault", "InvalidReference"])
    add('(dag:roots unknown-value)', ["dag:roots", "unknown-value"])
    code.append('(= (dag:roots CustomDag) AuthoredRoots)')
    add('(dag:roots CustomDag)', "AuthoredRoots")
    add(f'''(let $h (dag:from-records (Dag:Forest (1 1)
         ((Binary:Leaf {empty}) (Binary:Node {empty} (0 0)))))
       (let $result (dag:fold-unique $h dag-effect-leaf dag-effect-node)
         (let $closed (dag:close $h) $result)))''', ["3", "3"])
    # A million-leaf interpretation stays a 21-record carrier and is folded
    # without materializing the exponentially larger unfolded expression.
    records = [("leaf", b"p" * 32, ())] + [("node", b"", (i - 1, i - 1)) for i in range(1, 21)]
    add(f'''(let $h (dag:from-records {records_wire([20, 20], records)})
       (case (dag:stats $h)
         (((Dag:Stats $nodes $edges $bytes)
           (let* (($logged (println! (DagRetained $bytes)))
                  ($values (dag:fold-unique $h dag-leaf-size dag-node-size))
                  ($lengths (dag:encoded-lengths $h)))
             (let $closed (dag:close $h) (Compact $nodes $edges (< $bytes (car-atom $lengths)) $values $lengths)))))))''',
        ["Compact", "21", "40", "true", [str(33 * 2**20)] * 2,
         [str(measured(records)[20])] * 2])

    # A source may already be pointer-shared, for example after unfolding an
    # owned forest. Import it without walking a million copies of each child.
    add(f'''(let* (($h (dag:from-records {records_wire([20], records)}))
                  ($tree (dag:unfold $h 20))
                  ($copy (dag:from-forest ($tree $tree)))
                  ($closed (dag:close $h)))
       (case (dag:stats $copy)
         (((Dag:Stats $nodes $edges $bytes)
           (let $lengths (dag:encoded-lengths $copy)
             (let $released (dag:close $copy) (Reimport $nodes $edges $lengths)))))))''',
        ["Reimport", "21", "40", [str(measured(records)[20])] * 2])
    deep = [("leaf", b"", ())] + [("node", b"", (i - 1,)) for i in range(1, 4097)]
    add(f'''(let* (($h (dag:from-records {records_wire([4096], deep)}))
                  ($tree (dag:unfold $h 4096))
                  ($copy (dag:from-forest ($tree)))
                  ($closed (dag:close $h)))
       (case (dag:stats $copy)
         (((Dag:Stats $nodes $edges $bytes)
           (let $released (dag:close $copy) (DeepReimport $nodes $edges))))))''',
        ["DeepReimport", "4097", "4096"])

    program = "\n".join(code) + "\n"
    check_controls(program)
    fixture = args.output / "dag.metta"
    fixture.write_text(program)
    run = subprocess.run([str(args.cetta.resolve()), "--lang", "petta", str(fixture.resolve())],
                         cwd=ROOT, text=True, capture_output=True, timeout=60)
    (args.output / "dag.log").write_text(run.stdout + run.stderr)
    actual = [form for form in parse_forms(run.stdout)
              if isinstance(form, list) and form and form[0] == "DagCase"]
    by_case = {form[1]: form for form in actual}
    mismatches = [{"case": i, "expected": expected, "actual": by_case.get(str(i))}
                  for i, expected in enumerate(wanted) if by_case.get(str(i)) != expected]
    retained_bytes = [int(form[1]) for form in parse_forms(run.stdout)
                      if isinstance(form, list) and form and form[0] == "DagRetained"]
    effect_visits = run.stdout.count("DAG-UNIQUE-VISIT")
    unimported = [form for form in parse_forms(run.stdout)
                  if isinstance(form, list) and form and form[0] == "DagUnimported"]
    declined = unimported == [["DagUnimported", ["dag:from-forest", []]]]
    receipt = {"passed": run.returncode == 0 and not mismatches and len(actual) == len(wanted) and effect_visits == 2 and declined,
               "checks": len(wanted), "answers": len(actual), "mismatches": mismatches,
               "unique_effect_visits": effect_visits, "large_case_retained_bytes": retained_bytes,
               "unimported_unreduced": declined,
               "scope": "exact payloads, forest order/multiplicity, owned handles, chronological wire validation and pure memo folds"}
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({**receipt, "mismatches": receipt["mismatches"][:1]}))
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
