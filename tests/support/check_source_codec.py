#!/usr/bin/env python3
"""Independent source frames, exact ST1 bytes and inert source ownership."""

from __future__ import annotations

import argparse
from copy import deepcopy
import json
import math
from pathlib import Path
import struct
import subprocess

from check_binary_codec import ROOT, observation, parse_forms, tensor
from check_binary_tree import packed
from check_ic_archive import q, syntax, uint
from check_ic_search import check_controls


READER = ["Syntax:Reader", q("petta"), q("1.0.6"), q("")]
TERM = ("either", ("Syntax:Variable", "Syntax:Literal", "Syntax:Expr"))
FORMS = {
    "Syntax:Document": (1, ["text", "Syntax:Reader", ("vector", "Syntax:Form")]),
    "Syntax:Reader": (2, ["text", "text", "text"]),
    "Syntax:Form": (3, [("vector", ("either", ("Syntax:PlainName", "Syntax:NameKey"))), TERM]),
    "Syntax:PlainName": (4, ["text"]),
    "Syntax:NameKey": (5, [TERM]),
    "Syntax:Variable": (6, ["natural"]),
    "Syntax:Literal": (7, ["symbol", "bytes"]),
    "Syntax:Expr": (8, [("vector", TERM)]),
}


def encode_value(kind, value):
    if kind == "text":
        return "leaf", json.loads(value).encode()
    if kind == "symbol":
        return "leaf", value.encode()
    if kind == "natural":
        return "leaf", uint(value)
    if kind == "bytes":
        return "leaf", value
    if isinstance(kind, tuple) and kind[0] == "vector":
        return "node", b"", [encode_value(kind[1], item) for item in value]
    if isinstance(kind, tuple):
        assert kind[0] == "either" and value[0] in kind[1]
        kind = value[0]
    assert value[0] == kind
    tag, fields = FORMS[kind]
    assert len(value) == len(fields) + 1
    return "node", uint(tag), [encode_value(field, item) for field, item in zip(fields, value[1:])]


def encode(document):
    return packed(encode_value("Syntax:Document", document))


def reference(text):
    forms = []
    for term in parse_forms(text):
        names, identities = [], {}

        def walk(value):
            if isinstance(value, list):
                return ["Syntax:Expr", [walk(child) for child in value]]
            if value.startswith("$"):
                name = value[1:]
                key = ("anonymous", len(names)) if name == "_" else name
                if key not in identities:
                    identities[key] = len(names)
                    names.append(["Syntax:PlainName", q(name)])
                return ["Syntax:Variable", str(identities[key])]
            if value.startswith('"'):
                kind, data = "String", json.loads(value, strict=False).encode()
            elif value in ("True", "False"):
                kind, data = "Symbol", value.lower().encode()
            else:
                try:
                    n = int(value)
                    kind, data = ("I64", struct.pack(">q", n)) if -(2**63) <= n < 2**63 else ("BigInt", str(n).encode())
                except ValueError:
                    try:
                        if "." not in value:
                            raise ValueError()
                        number = float(value)
                        if not math.isfinite(number):
                            raise ValueError()
                        kind, data = "F64", struct.pack(">d", number)
                    except ValueError:
                        kind, data = "Symbol", value.encode()
            return ["Syntax:Literal", kind, data]

        body = walk(term)
        forms.append(["Syntax:Form", names, body])
    return ["Syntax:Document", q("ST1"), READER, forms]


def text_arg(value):
    return f'(binary:from-utf8 {tensor(value.encode())})'


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cetta", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    check_controls((ROOT / "lib/petta/lib_source_codec.metta").read_text())
    code = ['!(println! (SourceUnimported (syntax:read "a")))',
            '!(import! &self "lib/petta/lib_source_codec.metta")', '''
(= (source-test $text)
   (let* (($document (syntax:read $text))
          ($restored (syntax:restore $document))
          ($again (syntax:freeze $restored))
          ($printed (syntax:write $document)))
     (case (syntax:pack $document)
       (((Binary:TreeBytes $bytes)
         (SourceObservation (tensor:inspect $bytes) (== $again $document)
           (== (syntax:unpack $bytes) (Syntax:Value $document))
           (== (syntax:read $printed) $document)))
        ($other $other)))))
''']
    expected = []

    def add(call, value):
        i = len(expected)
        code.append(f'!(println! (SourceCase {i} {call}))')
        expected.append(["SourceCase", str(i), value])

    samples = ["", "(a)", "()", "(f $x $x)", "($x $_ $_ $x)",
               "(= (f $x) (quote ($x &self)))", "(f $x)\n(f $x)",
               "(quote (lambda $x (pair $x $y)))", "(() () ($x))",
               "(Syntax:Expr Syntax:Literal Syntax:Variable)", '(λ $α "λ●□")',
               '!(println! "SOURCE-NEVER-RUN")',
               '(0 -1 1.0 -0.0 2.5 18446744073709551617 True False)',
               '(1.0e999 -1.0e999)', '(string "a\x00b")', '; comment\n(= (f $x) $x)\n(= (f $x) $x)']
    for source in samples:
        document = reference(source)
        add(f'(source-test {text_arg(source)})',
            ["SourceObservation", observation(encode(document)), "true", "true", "true"])
    for invalid in ("(f", "(f $x) )", '"unterminated'):
        add(f'(syntax:read {text_arg(invalid)})', ["Syntax:Fault", "ParseFailed"])

    valid = reference("(f $x $x)")
    bad_index = deepcopy(valid); bad_index[3][0][2][1][1][1] = "1"
    float_index = deepcopy(valid); float_index[3][0][2][1][1][1] = "0.0"
    unused = deepcopy(valid); unused[3][0][1].append(["Syntax:PlainName", q("unused")])
    noncanonical = reference("(f $x $y)")
    noncanonical[3][0][2][1][1][1], noncanonical[3][0][2][1][2][1] = "1", "0"
    bad_literal = reference("1.0"); bad_literal[3][0][2][2] = b"bad"
    bad_bool = reference("True"); bad_bool[3][0][2][1:] = ["Bool", b"\2"]
    runtime = deepcopy(valid); runtime[1] = "$badversion"
    bad_name = deepcopy(valid); bad_name[3][0][1][0][1] = q("x\0tail")
    key_var = deepcopy(valid); key_var[3][0][1][0] = ["Syntax:NameKey", ["Syntax:Variable", "0"]]
    for doc, reason in ((bad_index, "InvalidVariable"), (float_index, "InvalidVariable"),
                        (unused, "UnusedVariable"), (noncanonical, "NoncanonicalVariables"),
                        (bad_literal, "InvalidLiteral"), (bad_bool, "InvalidLiteral"),
                        (key_var, "InvalidVariableName")):
        add(f'(syntax:restore {syntax(doc)})', ["Syntax:Fault", reason])
    add(f'(let $badversion {text_arg("ST1" + chr(0) + "tail")} (syntax:restore {syntax(runtime)}))',
        ["Syntax:Fault", "InvalidDocument"])
    bad_name[3][0][1][0][1] = "$badname"
    add(f'(let $badname {text_arg("x" + chr(0) + "tail")} (syntax:restore {syntax(bad_name)}))',
        ["Syntax:Fault", "InvalidVariableName"])
    add(f'''(let* (($document {syntax(valid)}) ($first (syntax:restore $document))
                   ($second (syntax:restore $document)))
       (FreshSource (== $first $second) (== (syntax:freeze $first) (syntax:freeze $second))))''',
        ["FreshSource", "false", "true"])
    changed = deepcopy(valid); changed[2][3] = q("other-reader")
    add(f'(syntax:write {syntax(changed)})', ["Syntax:Fault", "ReaderChanged"])
    aliases = reference("(f $x $y)")
    aliases[3][0][1][1] = deepcopy(aliases[3][0][1][0])
    add(f'(source-codec:valid? {syntax(aliases)})', "true")
    add(f'(syntax:write {syntax(aliases)})', ["Syntax:Fault", "NotRoundtrippable"])
    add('(syntax:freeze (SourceSyntax forms:1))', ["syntax:freeze", ["SourceSyntax", "forms:1"]])
    code.append('(= (syntax:freeze CustomSource) CustomFrozen)')
    add('(syntax:freeze CustomSource)', "CustomFrozen")

    # Actual library sources, not generated renamings. This gate checks their
    # capture/export contract, not that a learner has discovered useful helpers.
    corpus = ["lib/petta/list.metta", "lib/petta/postings.metta", "lib/petta/tensor.metta"]
    for path in corpus:
        source = (ROOT / path).read_text()
        add(f'''(let* (($document (syntax:read {text_arg(source)}))
                       ($source (syntax:restore $document))
                       ($emitted (syntax:write $document)))
             (CorpusSource (== (syntax:freeze $source) $document)
               (== (syntax:read $emitted) $document)))''', ["CorpusSource", "true", "true"])

    program = "\n".join(code) + "\n"
    check_controls(program)
    fixture = args.output / "sources.metta"
    fixture.write_text(program)
    run = subprocess.run([str(args.cetta.resolve()), "--lang", "petta", str(fixture.resolve())],
                         cwd=ROOT, text=True, capture_output=True, timeout=60)
    (args.output / "sources.log").write_text(run.stdout + run.stderr)
    forms = parse_forms(run.stdout)
    actual = [f for f in forms if isinstance(f, list) and f and f[0] == "SourceCase"]
    by_case = {f[1]: f for f in actual}
    mismatches = [{"case": i, "expected": value, "actual": by_case.get(str(i))}
                  for i, value in enumerate(expected) if by_case.get(str(i)) != value]
    unimported = [f for f in forms if isinstance(f, list) and f and f[0] == "SourceUnimported"]
    inert = all(line.strip() != '"SOURCE-NEVER-RUN"' and line.strip() != "SOURCE-NEVER-RUN"
                for line in run.stdout.splitlines())
    absent = unimported == [["SourceUnimported", ["syntax:read", q("a")]]]
    receipt = {"passed": run.returncode == 0 and not mismatches and len(actual) == len(expected) and inert and absent,
               "checks": len(expected), "answers": len(actual), "mismatches": mismatches,
               "unimported_unreduced": absent,
               "source_execution_absent": inert, "corpus": corpus,
               "scope": "exact ST1 bytes, source-variable aliases, inert owned syntax and current-reader round trips"}

    # A bounded connection check: written source is accepted by the actual
    # loader and retains pure, branching, failure and effect observations.
    snippets = [
        '(= (twice $x) (+ $x $x))\n!(println! (twice 7))\n',
        '(= (pick) 3)\n(= (pick) 5)\n!(println! (collapse (pick)))\n'
        '!(println! "effect-once")\n!(println! (collapse (superpose ())))\n',
    ]
    emitter = args.output / "emit.metta"
    emitter.write_text('!(import! &self "lib/petta/lib_source_codec.metta")\n' + "\n".join(
        f'!(println! (SourceEmission {i} (tensor:inspect (binary:utf8 (syntax:write (syntax:read {text_arg(s)}))))))'
        for i, s in enumerate(snippets)) + "\n")
    emit = subprocess.run([str(args.cetta.resolve()), "--lang", "petta", str(emitter.resolve())],
                          cwd=ROOT, text=True, capture_output=True, timeout=30)
    (args.output / "emit.log").write_text(emit.stdout + emit.stderr)
    written = [f for f in parse_forms(emit.stdout) if isinstance(f, list) and f and f[0] == "SourceEmission"]
    execution = []
    for i, snippet in enumerate(snippets):
        item = next((f for f in written if f[1] == str(i)), None)
        emitted = None
        if item and isinstance(item[2], list) and item[2][:2] == ["TensorData", "u8"]:
            emitted = bytes(int(row[1]) for row in item[2][3]).decode()
        outputs = []
        for name, content in (("original", snippet), ("emitted", emitted)):
            if content is None:
                outputs.append((1, []))
                continue
            path = args.output / f"execute-{i}-{name}.metta"
            path.write_text(content)
            checked = subprocess.run([str(args.cetta.resolve()), "--lang", "petta", str(path.resolve())],
                                     cwd=ROOT, text=True, capture_output=True, timeout=30)
            (args.output / f"execute-{i}-{name}.log").write_text(checked.stdout + checked.stderr)
            outputs.append((checked.returncode, parse_forms(checked.stdout)))
        execution.append({"case": i, "passed": outputs[0][0] == 0 and outputs[0] == outputs[1]})
    prime_fixture = args.output / "prime.metta"
    prime_fixture.write_text('''!(import! &self syntax)
!(let $document (syntax:read "(= (f $x) (quote ($x &self)))
!(println! 1)")
   (PrimeSource
     (== (syntax:freeze (syntax:restore $document)) $document)
     (== (syntax:read (syntax:write $document)) $document)))
''')
    prime = subprocess.run([str(args.cetta.resolve()), "--lang", "prime", str(prime_fixture.resolve())],
                           cwd=ROOT, text=True, capture_output=True, timeout=30)
    (args.output / "prime.log").write_text(prime.stdout + prime.stderr)
    prime_ok = prime.returncode == 0 and ["PrimeSource", "True", "True"] in parse_forms(prime.stdout)
    receipt.update({"execution": execution, "prime_reader_connection": prime_ok})
    receipt["passed"] &= emit.returncode == 0 and len(written) == len(snippets) and all(
        row["passed"] for row in execution) and prime_ok
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({**receipt, "mismatches": [{"case": m["case"]} for m in mismatches]}))
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
