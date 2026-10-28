#!/usr/bin/env python3
"""Independent byte-tree catalogues and checked real-source template reuse."""

from __future__ import annotations

import argparse
from copy import deepcopy
import itertools
import json
from pathlib import Path
import subprocess

from check_binary_codec import ROOT, encode as frame, observation, parse_forms, tensor
from check_binary_tree import inspect, wire
from check_compact_dag import compact
from check_ic_archive import encode, node, q, source, syntax
from check_ic_grammar import factor
from check_ic_search import check_controls
from check_ic_templates import generalized
from check_source_codec import encode as source_encode, encode_value, reference, text_arg


SLOT = ["IC:Slot", "Root", "ByteTree"]


def envelope(codec, payload, version=b"SIC1"):
    return frame([version, codec, payload])


def archive(forest):
    """A separate implementation of catalogue construction, not table parsing."""
    roots, records = compact(forest)
    keys, codes = [], []
    for kind, payload, children in records:
        descriptors = [("bytes", payload)] if kind == "leaf" else [
            ("arity", len(children)), ("bytes", payload)]
        for descriptor in descriptors:
            if descriptor not in keys:
                keys.append(descriptor)
            codes.append(keys.index(descriptor))
    signature, heads, literals = [], [], []
    for kind, value in keys:
        if kind == "bytes":
            signature.append(["IC:Operator", "ByteTree", []])
            heads.append(["IC:LiteralRef", str(len(literals))])
            literals.append(["IC:Literal", "U8", value])
        else:
            signature.append(["IC:Operator", "ByteTree", [["IC:Slot", "Value", "ByteTree"]] * (value + 1)])
            heads.append(["IC:Name", q("Binary:Node")])
    values = []
    at = 0
    for kind, payload, children in records:
        code = codes[at]
        at += 1
        arguments = []
        if kind != "leaf":
            arguments = [node(codes[at])] + [values[i] for i in children]
            at += 1
        values.append(node(code, *arguments))
    result = ["IC:Archive", q("IC1"), ["IC:Observation", "ExactSyntax", b""], signature,
              heads, literals, [], [["IC:Raw", [SLOT], [values[i]]] for i in roots]]
    assert at == len(codes)
    source(result)
    return result


def source_forest(document):
    header = deepcopy(document)
    header[3] = []
    return [encode_value("Syntax:Document", header)] + [encode_value("Syntax:Form", f) for f in document[3]]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cetta", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for name in ("lib_ic_tree_archive.metta", "lib_ic_source_archive.metta"):
        check_controls((ROOT / "lib/petta" / name).read_text())
    code = ['!(import! &self (library lib_ic_source_archive))', '''
(= (tree-archive:view $tree)
   (case $tree
     (((Binary:Leaf $bytes) (Binary:Leaf (tensor:inspect $bytes)))
      ((Binary:Node $tag $children)
       (Binary:Node (tensor:inspect $tag) (map-atom $children $child (tree-archive:view $child)))))))
(= (tree-archive:test $forest)
   (case (ic:prepare-trees $forest)
     (((IC:TreeArchive $archive)
       (case ((ic-archive:pack $archive) (ic:restore-trees $archive))
         ((((Binary:TreeBytes $bytes) (IC:Some $trees))
           (TreeArchiveObservation (tensor:inspect $bytes)
             (map-atom $trees $tree (tree-archive:view $tree)))))))
      ($other $other))))
(= (source-archive:test $text)
   (let $document (syntax:read $text)
     (case (ic:prepare-source $document)
       (((IC:TreeArchive $archive)
         (case (ic-archive:pack $archive)
           (((Binary:TreeBytes $bytes)
             (SourceArchiveObservation (tensor:inspect $bytes)
               (== (ic:restore-source $archive) (IC:Some $document)))))))))))
''']
    expected = []

    def add(call, result):
        i = len(expected)
        code.append(f'!(println! (SourceArchiveCase {i} {call}))')
        expected.append(["SourceArchiveCase", str(i), result])

    leaf = ("leaf", b"x\0\xff")
    forests = [[], [leaf], [leaf, leaf], [("node", b"", [])],
               [leaf, ("node", leaf[1], [])],
               [("node", b"left", [leaf, leaf]), ("node", b"right", [leaf])],
               [("node", b"\0", [("node", b"\0", []), leaf])]]
    for forest in forests:
        want = archive(forest)
        add('(tree-archive:test (' + " ".join(map(wire, forest)) + '))',
            ["TreeArchiveObservation", observation(encode(want)), list(map(inspect, forest))])
    examples = ["", "()", "(f $x $x)", "($x $_ $_ $x)",
                '(= (f $x) (quote ($x &self)))\n!(println! "NEVER-EXECUTE-ARCHIVE")',
                '(value "a\0b" -0.0 1.0)\n(value "a\0b" -0.0 1.0)']
    for text in examples:
        want = archive(source_forest(reference(text)))
        add(f'(source-archive:test {text_arg(text)})',
            ["SourceArchiveObservation", observation(encode(want)), "true"])
    add('(ic:prepare-source (Syntax:Document "bad"))', ["IC:TreeFault", "InvalidSource"])
    add('(ic:prepare-trees ((Binary:Leaf "not bytes")))', ["Dag:Fault", "InvalidPayload"])
    add('(ic:restore-source ' + syntax(archive([])) + ')', "IC:None")
    add('(ic:restore-source ' + syntax(archive([leaf])) + ')', "IC:None")

    # Real authored wrappers, selected by function family rather than duplicated
    # generated examples. This checks representation reuse, not call inlining.
    library = ROOT / "lib/petta/list.metta"
    names = {"list:remove-all-by", "list:unique-by", "list:take", "list:drop"}
    definitions = [f for f in parse_forms(library.read_text()) if isinstance(f, list) and len(f) == 3
                   and f[0] == "=" and isinstance(f[1], list) and f[1][0] in names]
    assert len(definitions) == len(names)
    real_text = "\n".join(syntax(f) for f in definitions)
    document = reference(real_text)
    original = archive(source_forest(document))
    packets = source(original)
    candidates = []
    for left, right in itertools.combinations(range(1, len(packets)), 2):
        patterns = generalized(original[3], [SLOT], packets[left][2], packets[right][2])[1]
        candidate, count = factor(original, [SLOT], patterns)
        candidates.append((len(encode(candidate)), left, right, count, candidate))
    size, left, right, support, candidate = min(candidates, key=lambda row: row[0])
    assert size < len(encode(original)) and support >= 2
    add(f'''(let* (($document (syntax:read {text_arg(real_text)}))
                     ($prepared (ic:prepare-source $document)))
      (case $prepared
        (((IC:TreeArchive $original)
          (case (ic-archive:learn-pair $original $original "common" {left} {right})
            (((IC:ArchiveAccepted $candidate $bytes $before $after)
              (case (ic:restore-source $candidate)
                (((IC:Some $restored)
                  (RealSourceTemplate $before $after (== $restored $document)
                    (== (syntax:read (syntax:write $restored)) $document))))))
             ($other $other))))
         ($other $other))))''',
        ["RealSourceTemplate", str(len(encode(original))), str(size), "true", "true"])
    damaged = deepcopy(candidate)
    # A changed variable ordinal is an observation change, not alpha-renaming.
    damaged[7][0] = ["IC:Raw", [SLOT], [node(0)]]
    add(f'(ic-archive:check {syntax(original)} {syntax(original)} {syntax(damaged)})',
        ["IC:ArchiveRejected", "Reconstruction"])

    plain = envelope(b"ST1", source_encode(document))
    learned = envelope(b"IC1", encode(candidate))
    assert len(learned) > len(plain)
    add(f'(ic:source-check {tensor(plain)} {tensor(plain)} {tensor(learned)})',
        ["IC:SourceRejected", "NotSmaller"])
    add(f'(ic:source-check {tensor(plain)} {tensor(plain)} {tensor(plain)})',
        ["IC:SourceRejected", "NotSmaller"])
    changed_source = envelope(b"ST1", source_encode(reference("(different)")))
    add(f'(ic:source-check {tensor(plain)} {tensor(plain)} {tensor(changed_source)})',
        ["IC:SourceRejected", "Reconstruction"])
    for invalid in (envelope(b"unknown", b""), envelope(b"ST1", b""),
                    envelope(b"ST1", source_encode(document), b"bad"),
                    frame([b"SIC1", b"ST1", source_encode(document), b"extra"])):
        add(f'(ic:source-unpack {tensor(invalid)})', "IC:None")
    add(f'''(let $document (syntax:read {text_arg(real_text)})
       (== (ic:source-unpack (ic:source-pack $document)) (IC:Some $document)))''', "true")
    small_doc = reference("(f $x $x)")
    small_archive = archive(source_forest(small_doc))
    add(f'(tensor:inspect (ic:source-pack (syntax:read {text_arg("(f $x $x)")})))',
        observation(envelope(b"ST1", source_encode(small_doc))))
    add(f'(tensor:inspect (ic:source-from-archive {syntax(small_archive)}))',
        observation(envelope(b"IC1", encode(small_archive))))

    # A positive representation control: duplicate authored rules remain
    # duplicate observations. The small corpus above stays in its cheaper
    # source codec; this repeated one genuinely benefits from a template.
    repeated_text = "\n".join([syntax(max(definitions, key=lambda f: len(syntax(f))))] * 12)
    repeated_doc = reference(repeated_text)
    repeated_archive = archive(source_forest(repeated_doc))
    packets = source(repeated_archive)
    pattern = generalized(repeated_archive[3], [SLOT], packets[1][2], packets[2][2])[1]
    repeated_candidate, repeated_support = factor(repeated_archive, [SLOT], pattern)
    repeated_plain = envelope(b"ST1", source_encode(repeated_doc))
    repeated_learned = envelope(b"IC1", encode(repeated_candidate))
    assert repeated_support == 12 and len(repeated_learned) < len(repeated_plain)
    add(f'''(case (ic:source-check {tensor(repeated_plain)} {tensor(repeated_plain)} {tensor(repeated_learned)})
      (((IC:SourceAccepted $bytes $before $after)
        (SourceGain $before $after (== (ic:source-unpack $bytes)
          (ic:source-unpack {tensor(repeated_plain)}))))
       ($other $other)))''',
        ["SourceGain", str(len(repeated_plain)), str(len(repeated_learned)), "true"])
    for before, proposal, wins in ((plain, learned, "false"), (repeated_plain, repeated_learned, "true")):
        add(f'''(let* (($original {tensor(before)}) ($candidate {tensor(proposal)})
                       ($handle (work-new (ic-search:from-list ((IC:Candidate $candidate))))))
          (case (ic:prepare IC:SourceArchive $original () 256 ((IC:Branch 1 $handle)))
            (((IC:PreparedSearch $state)
              (case (ic:resume $state 100)
                (((IC:SearchResult $best (IC:Statistics $turns $seen $accepted $faults) $reason $fault $next)
                  (SourceSearch (== $best $candidate) $reason $seen $accepted $faults)))))
             ($other $other))))''',
            ["SourceSearch", wins, "RegionExhausted", "1", "1" if wins == "true" else "0", "0"])

    # A source template is not automatically an executable helper. In ordinary
    # PeTTa, a new call boundary evaluates its arguments before the body. These
    # controls distinguish raw code from values already established at entry.
    code += ['''
(= (source-helper:unused $argument) 0)
(= (source-helper:twice $value) (+ $value $value))
(= (source-helper:original $value) (+ $value $value))
(= (source-helper:factored $value) (source-helper:twice $value))
(= (source-helper:choice-counts $answers)
   (ChoiceCounts (size-atom $answers)
     (size-atom (filter-atom $answers $answer (== $answer 3)))))
''']
    add('''(let $space (new-space)
      (let* (($direct (if True 0 (add-atom $space Visited)))
              ($before (size-atom (collapse (match $space Visited Counted))))
              ($helper (source-helper:unused (add-atom $space Visited))))
        (UnusedArgument $direct $helper $before
          (size-atom (collapse (match $space Visited Counted))))))''',
        ["UnusedArgument", "0", "0", "0", "1"])
    add('''(ChoiceBoundary
      (source-helper:choice-counts (collapse (+ (superpose (1 2)) (superpose (1 2)))))
      (source-helper:choice-counts (collapse (source-helper:twice (superpose (1 2))))))''',
        ["ChoiceBoundary", ["ChoiceCounts", "4", "2"], ["ChoiceCounts", "2", "0"]])
    add('''(FailureBoundary (size-atom (collapse (if True 0 (superpose ()))))
      (size-atom (collapse (source-helper:unused (superpose ())))))''',
        ["FailureBoundary", "1", "0"])
    add('''(EstablishedValues
      (source-helper:choice-counts (collapse (source-helper:original (superpose (1 2)))))
      (source-helper:choice-counts (collapse (source-helper:factored (superpose (1 2))))))''',
        ["EstablishedValues", ["ChoiceCounts", "2", "0"], ["ChoiceCounts", "2", "0"]])

    program = "\n".join(code) + "\n"
    check_controls(program)
    fixture = args.output / "sources.metta"
    fixture.write_text(program)
    run = subprocess.run([str(args.cetta.resolve()), "--lang", "petta", str(fixture.resolve())],
                         cwd=ROOT, text=True, capture_output=True, timeout=60)
    (args.output / "sources.log").write_text(run.stdout + run.stderr)
    actual = [f for f in parse_forms(run.stdout) if isinstance(f, list) and f and f[0] == "SourceArchiveCase"]
    found = {f[1]: f for f in actual}
    differences = [{"case": i, "expected": want, "actual": found.get(str(i))}
                   for i, want in enumerate(expected) if found.get(str(i)) != want]
    inert = all(line.strip() not in ("NEVER-EXECUTE-ARCHIVE", q("NEVER-EXECUTE-ARCHIVE"))
                for line in run.stdout.splitlines())
    receipt = {"passed": run.returncode == 0 and not differences and len(actual) == len(expected) and inert,
               "checks": len(expected), "answers": len(actual), "mismatches": differences,
               "source_execution_absent": inert,
               "source_family": "list/combinator authored wrappers",
               "factoring": {"before_bytes": len(encode(original)), "after_bytes": size,
                             "source_forms": len(definitions), "support": support,
                             "plain_source_bytes": len(source_encode(document)),
                             "net_gain_accepted": False},
               "repeated_source": {"source_forms": repeated_support, "before_bytes": len(repeated_plain),
                                   "after_bytes": len(repeated_learned)},
               "helper_boundary_controls": 4,
               "unchecked_helper_extraction_safe": False,
               "scope": "exact source reconstruction and cross-codec complete cost; not binder-aware inlining"}
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({**receipt, "mismatches": [{"case": d["case"]} for d in differences]}))
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
