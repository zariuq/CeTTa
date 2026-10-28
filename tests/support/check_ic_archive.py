#!/usr/bin/env python3
"""Independent IC1 wire, reconstruction and complete-cost controls."""

from __future__ import annotations

import argparse
from copy import deepcopy
import json
from pathlib import Path
import struct
import subprocess

from check_binary_codec import ROOT, observation, parse_forms, tensor
from check_binary_tree import packed
from check_ic_templates import node, hole, reference


def q(value):
    return json.dumps(value, ensure_ascii=False)


def vector(kind):
    return ("vector", kind)


def record(*names):
    return ("record", names)


PATTERN = record("IC:Node", "IC:Hole")
SLOTS = vector(record("IC:Slot"))
# Independent statement of IC1's layout, not read from executable tables.
FORMS = {
    "IC:Archive": (1, ["text", record("IC:Observation"), vector(record("IC:Operator")),
                        vector(record("IC:Name", "IC:LiteralRef")), vector(record("IC:Literal")),
                        vector(record("IC:Definition")), vector(record("IC:Raw", "IC:Call"))]),
    "IC:Observation": (2, ["symbol", "bytes"]),
    "IC:Operator": (3, ["symbol", SLOTS]),
    "IC:Slot": (4, ["symbol", "symbol"]),
    "IC:Name": (5, ["text"]),
    "IC:LiteralRef": (6, ["natural"]),
    "IC:Literal": (7, ["symbol", "bytes"]),
    "IC:Definition": (8, ["text", SLOTS, vector(PATTERN)]),
    "IC:Node": (9, ["natural", vector(PATTERN)]),
    "IC:Hole": (10, ["symbol", "natural"]),
    "IC:Raw": (11, [SLOTS, vector(record("IC:Node"))]),
    "IC:Call": (12, ["natural", vector(record("IC:Binding"))]),
    "IC:Binding": (13, ["symbol", "natural", record("IC:Node")]),
}


def uint(value):
    number = int(value)
    return number.to_bytes((number.bit_length() + 7) // 8, "big")


def tree(kind, value):
    if kind == "bytes":
        return "leaf", value
    if kind == "text":
        return "leaf", json.loads(value).encode()
    if kind == "symbol":
        return "leaf", value.encode()
    if kind == "natural":
        return "leaf", uint(value)
    if kind[0] == "vector":
        return "node", b"", [tree(kind[1], x) for x in value]
    assert kind[0] == "record" and value[0] in kind[1]
    tag, fields = FORMS[value[0]]
    assert len(fields) == len(value) - 1
    return "node", uint(tag), [tree(k, x) for k, x in zip(fields, value[1:])]


def encode(archive):
    return packed(tree(record("IC:Archive"), archive))


def source(archive):
    _, version, obs, signature, heads, literals, definitions, items = archive
    assert version == q("IC1") and obs == ["IC:Observation", "ExactSyntax", b""]
    assert len(signature) == len(heads)
    for literal in literals:
        _, kind, payload = literal
        assert isinstance(payload, bytes)
        assert kind in ("U8", "I64", "F64", "String", "Symbol")
        if kind in ("I64", "F64"):
            assert len(payload) == 8
        if kind in ("String", "Symbol"):
            payload.decode()
        if kind == "Symbol":
            assert b"\0" not in payload
    for operator, head in zip(signature, heads):
        if head[0] == "IC:LiteralRef":
            assert not operator[2] and 0 <= int(head[1]) < len(literals)

    def expand(pattern, env):
        if pattern[0] == "IC:Hole":
            return deepcopy(env[pattern[1], int(pattern[2])])
        return node(pattern[1], *[expand(x, env) for x in pattern[2]])

    packets = []
    for item in items:
        if item[0] == "IC:Raw":
            _, slots, values = item
            checked = reference(signature, slots, values, values)
            assert checked[0] == "TemplateObservation" and not checked[1]
        else:
            assert item[0] == "IC:Call"
            _, _, slots, patterns = definitions[int(item[1])]
            env = {}
            for _, role, index, value in item[2]:
                key = role, int(index)
                assert key not in env
                env[key] = value
            values = [expand(pattern, env) for pattern in patterns]
            checked = reference(signature, slots, patterns, values)
            assert checked[0] == "TemplateObservation"
            required = {(role, int(index)): value for _, role, index, value in checked[1]}
            assert required == env
        packets.append(["IC:Packet", slots, values])
    return packets


def syntax(value):
    if isinstance(value, bytes):
        return tensor(value)
    if isinstance(value, list):
        return "(" + " ".join(map(syntax, value)) + ")"
    return value


def viewed(value):
    if isinstance(value, bytes):
        return observation(value)
    return [viewed(x) for x in value] if isinstance(value, list) else value


def checked(original, current, candidate):
    if original[:6] != current[:6] or original[:6] != candidate[:6]:
        return ["IC:ArchiveRejected", "Catalog"]
    if source(original) != source(current) or source(original) != source(candidate):
        return ["IC:ArchiveRejected", "Reconstruction"]
    before, after = encode(current), encode(candidate)
    if len(after) >= len(before):
        return ["IC:ArchiveRejected", "NotSmaller"]
    return ["AcceptedObservation", viewed(candidate), observation(after), str(len(before)), str(len(after))]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cetta", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    signature = [["IC:Operator", "Term", []], ["IC:Operator", "Term", []],
                 ["IC:Operator", "Term", [["IC:Slot", "Value", "Term"], ["IC:Slot", "Value", "Term"]]]]
    heads = [["IC:Name", q("zero")], ["IC:Name", q("one")], ["IC:Name", q("pair")]]
    slots = [["IC:Slot", "Root", "Term"]]

    def body(value, depth=3):
        return value if depth == 0 else node(2, body(value, depth - 1), body(value, depth - 1))

    pattern = body(hole("Value", 0))
    inputs = [node(0), node(1)] * 4 + [node(2, node(0), node(1))] * 2
    original = ["IC:Archive", q("IC1"), ["IC:Observation", "ExactSyntax", b""], signature, heads, [], [],
                [["IC:Raw", slots, [body(value)]] for value in inputs]]
    candidate = deepcopy(original)
    candidate[6] = [["IC:Definition", q("shared"), slots, [pattern]]]
    candidate[7] = [["IC:Call", "0", [["IC:Binding", "Value", "0", value]]] for value in inputs]
    assert source(original) == source(candidate)
    assert len(encode(candidate)) < len(encode(original))
    literal_archive = deepcopy(original)
    literal_archive[5] = [["IC:Literal", "String", "λ\0□".encode()],
                          ["IC:Literal", "F64", struct.pack(">d", 1.25)],
                          ["IC:Literal", "I64", struct.pack(">q", -7)],
                          ["IC:Literal", "Symbol", b"(not-a-call)"],
                          ["IC:Literal", "U8", bytes(range(8))]]
    literal_archive[4][0] = ["IC:LiteralRef", "0"]
    literal_archive[7] = literal_archive[7][:1]
    small = deepcopy(original)
    small[7] = small[7][:2]
    small[7][1] = deepcopy(small[7][0])
    expensive = deepcopy(small)
    expensive[6] = [["IC:Definition", q("large-name-" * 500), slots, [body(node(0))]]]
    expensive[7] = [["IC:Call", "0", []], ["IC:Call", "0", []]]
    # 30 source nodes become 15 definition nodes plus 2 calls, but bytes grow.
    assert source(small) == source(expensive)
    assert len(encode(expensive)) > len(encode(small))
    code = ['!(import! &self (library lib_ic_archive))', '''
(= (archive-view $archive)
   (case $archive
     (((IC:Archive $version (IC:Observation $kind $bytes) $signature $heads $literals $definitions $items)
       (IC:Archive $version (IC:Observation $kind (tensor:inspect $bytes)) $signature $heads
         (map-atom $literals $literal
           (case $literal (((IC:Literal $type $payload) (IC:Literal $type (tensor:inspect $payload))))))
         $definitions $items)))))
(= (archive-test $archive)
   (case (ic-archive:pack $archive)
     (((Binary:TreeBytes $bytes)
       (case (ic-archive:unpack $bytes)
         (((IC:Some $decoded)
           (ArchiveObservation (tensor:inspect $bytes) (archive-view $decoded) (ic-archive:restore $decoded)))
          ($other $other))))
      ($other $other))))
(= (archive-result $result)
   (case $result
     (((IC:ArchiveAccepted $archive $bytes $before $after)
       (AcceptedObservation (archive-view $archive) (tensor:inspect $bytes) $before $after))
      ($other $other))))
(= (archive-check $original $current $candidate)
   (archive-result (ic-archive:check $original $current $candidate)))
''']
    expected = []

    def add(call, result):
        index = len(expected)
        code.append(f"!(println! (ArchiveCase {index} {call}))")
        expected.append(["ArchiveCase", str(index), result])

    for archive in (original, candidate, literal_archive, small, expensive):
        add(f"(archive-test {syntax(archive)})",
            ["ArchiveObservation", observation(encode(archive)), viewed(archive), ["IC:Some", source(archive)]])
    add(f"(archive-check {syntax(original)} {syntax(original)} {syntax(candidate)})", checked(original, original, candidate))
    add(f"(archive-result (ic-archive:install {syntax(original)} {syntax(original)} {q('shared')} {syntax(slots)} {syntax([pattern])}))",
        checked(original, original, candidate))
    add(f"(archive-result (ic-archive:learn-pair {syntax(original)} {syntax(original)} {q('shared')} 0 1))",
        checked(original, original, candidate))
    add(f"(ic-archive:learn-pair {syntax(original)} {syntax(original)} {q('shared')} 0 999)",
        ["IC:ArchiveRejected", "InvalidSelection"])
    add(f"(ic-archive:learn-pair {syntax(original)} {syntax(original)} {q('shared')} 1.0 1)",
        ["IC:ArchiveRejected", "InvalidSelection"])
    add(f"(archive-check {syntax(small)} {syntax(small)} {syntax(expensive)})", checked(small, small, expensive))
    add(f"(archive-check {syntax(original)} {syntax(candidate)} {syntax(candidate)})", checked(original, candidate, candidate))
    mismatch = deepcopy(candidate)
    mismatch[7][0][2][0][3] = node(1)
    add(f"(archive-check {syntax(original)} {syntax(original)} {syntax(mismatch)})", checked(original, original, mismatch))
    catalog = deepcopy(candidate)
    catalog[4][0][1] = q("changed")
    add(f"(archive-check {syntax(original)} {syntax(original)} {syntax(catalog)})", checked(original, original, catalog))

    invalid = []
    for field, replacement in ((1, q("IC2")), (2, ["IC:Observation", "Unknown", b""]),
                                (4, []), (5, [["IC:Literal", "I64", b"\0"]])):
        bad = deepcopy(candidate)
        bad[field] = replacement
        invalid.append(bad)
    for bindings in ([], [["IC:Binding", "Code", "0", node(0)]],
                     [["IC:Binding", "Value", "0", node(0)]] * 2,
                     [["IC:Binding", "Value", "0", node(0)], ["IC:Binding", "Value", "1", node(1)]]):
        bad = deepcopy(candidate)
        bad[7][0][2] = bindings
        invalid.append(bad)
    bad = deepcopy(candidate)
    bad[7][0][1] = "999"
    invalid.append(bad)
    for bad in invalid:
        add(f"(ic-archive:pack {syntax(bad)})", ["Binary:TreeFault", "InvalidArchive"])
        # Structurally valid bytes still pass through semantic admission on read.
        add(f"(ic-archive:unpack {tensor(encode(bad))})", ["Binary:TreeFault", "InvalidArchive"])
    for schema in ('((Binary:Form 1 A ()) (Binary:Form 1 B ()))',
                   '((Binary:Form 1 A ()) (Binary:Form 2 A ()))',
                   '((Binary:Form 1 A ((Record Missing))))',
                   '((Binary:Form 1 A ()) malformed)'):
        add(f"(binary-schema:encode {schema} (Record A) (A))", ["Binary:TreeFault", "InvalidSchema"])
    fixture = args.output / "archives.metta"
    fixture.write_text("\n".join(code) + "\n")
    result = subprocess.run([str(args.cetta.resolve()), "--lang", "petta", str(fixture.resolve())],
                            cwd=ROOT, capture_output=True, text=True, timeout=60)
    (args.output / "archives.log").write_text(result.stdout + result.stderr)
    actual = [x for x in parse_forms(result.stdout) if isinstance(x, list) and x and x[0] == "ArchiveCase"]
    wrong = [i for i, (a, b) in enumerate(zip(actual, expected)) if a != b]
    receipt = {"passed": result.returncode == 0 and len(actual) == len(expected) and not wrong,
               "checks": len(expected), "answers": len(actual), "mismatches": wrong,
               "source_bytes": len(encode(original)), "factored_bytes": len(encode(candidate)),
               "node_smaller_byte_larger_rejected": checked(small, small, expensive) == ["IC:ArchiveRejected", "NotSmaller"],
               "scope": "first-order IC1 archive; exact complete bytes, typed fields, original-task reconstruction and full-cost acceptance"}
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt))
    if not receipt["passed"]:
        for i in wrong[:4]:
            print("expected", repr(expected[i])[:1400])
            print("actual  ", repr(actual[i])[:1400])
        print(result.stderr[-2000:])
        raise SystemExit(1)


if __name__ == "__main__":
    main()
