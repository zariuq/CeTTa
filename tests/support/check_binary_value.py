#!/usr/bin/env python3
"""Independent protocol-value bytes, inert reconstruction and authority refusal."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess

from check_binary_codec import ROOT, observation, parse_forms, tensor
from check_binary_tree import packed
from check_ic_archive import uint
from check_ic_search import check_controls


class Text(str):
    pass


def encoded_value(value):
    if isinstance(value, bytes):
        tag, payload = 4, value
    elif isinstance(value, Text):
        tag, payload = 2, value.encode()
    elif isinstance(value, str):
        tag, payload = 1, value.encode()
    elif type(value) is int:
        tag, payload = 3, uint(value)
    else:
        assert isinstance(value, list)
        return "node", b"\5", [("node", b"", [encoded_value(v) for v in value])]
    return "node", uint(tag), [("leaf", payload)]


def encoded(value, version=b"BV1"):
    return packed(("node", b"\6", [("leaf", version), encoded_value(value)]))


def construct(value):
    if isinstance(value, Text):
        return f"(binary:from-utf8 {tensor(value.encode())})"
    if isinstance(value, bytes):
        return tensor(value)
    if isinstance(value, str):
        return value
    if type(value) is int:
        return str(value)
    result = "()"
    for item in reversed(value):
        result = f"(cons-atom {construct(item)} {result})"
    return result


def view(value):
    if isinstance(value, bytes):
        return ["ValueBytes", observation(value)]
    if isinstance(value, Text):
        return ["ValueText", observation(value.encode())]
    if isinstance(value, str):
        return ["ValueSymbol", observation(value.encode())]
    if type(value) is int:
        return ["ValueNatural", str(value)]
    return ["ValueExpression", [view(v) for v in value]]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cetta", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    check_controls((ROOT / "lib/petta/lib_binary_value.metta").read_text())
    program = ['!(import! &self (library lib_binary_value))', '''
(= (value-view $value)
   (case (binary:value-kind $value)
     ((Symbol (ValueSymbol (tensor:inspect (binary:symbol-utf8 $value))))
      (Text (ValueText (tensor:inspect (binary:utf8 $value))))
      (Natural (ValueNatural $value))
      (Bytes (ValueBytes (tensor:inspect $value)))
      (Expression (ValueExpression (map-atom $value $child (value-view $child))))
      ($other $other))))
(= (value-test $value)
   (case (binary-value:pack $value)
     (((Binary:TreeBytes $bytes)
       (case (binary-value:unpack $bytes)
         (((BinaryValue:Value $restored)
           (ValueObservation (tensor:inspect $bytes) (value-view $restored)))
          ($other $other))))
      ($other $other))))
''']
    expected = []

    def add(call, wanted):
        i = len(expected)
        program.append(f"!(println! (ValueCase {i} {call}))")
        expected.append(["ValueCase", str(i), wanted])

    values = ["name", Text("name"), 0, 256, 2**63 - 1, b"", b"\0\xff",
              Text("λ\0□"), [], ["a", "a", Text("a"), 0],
              ["Wire:Pair", ["Wire:Atom", 0], ["Wire:Atom", 9]],
              ["println!", Text("BVALUE-MUST-NOT-RUN")],
              ["quote", ["println!", Text("BVALUE-MUST-NOT-RUN")]],
              ["NativeHandle", Text("dag"), 1]]
    for value in values:
        add(f"(value-test {construct(value)})", ["ValueObservation", observation(encoded(value)), view(value)])
    # Decoding hostile call-shaped data must remain inert through a function
    # argument, let and case, not only while it is printed at the outer boundary.
    for value in values[-3:]:
        add(f'''(case (binary-value:unpack {tensor(encoded(value))})
          (((BinaryValue:Value $value) (let $copy $value (value-view $copy))) ($other $other)))''', view(value))
    for bad in ("-1", "0.0", "1.5", "$unbound", "(new-space)"):
        add(f"(binary-value:pack {bad})", ["BinaryValue:Fault", "UnsupportedValue"])
    for bytes_arg, raw in (
        ("(tensor:view (tensor:from-values u8 (3) (10 20 30)) (3) (-1) 2)", bytes([30, 20, 10])),
        ("(tensor:from-coo u8 (2) ((0 0) (1 255)))", bytes([0, 255])),
    ):
        add(f"(value-test {bytes_arg})", ["ValueObservation", observation(encoded(raw)), view(raw)])
    add("(binary-value:pack (tensor:from-coo u8 (2) ((0 0))))", ["BinaryValue:Fault", "UnsupportedValue"])
    add(f"(binary-value:unpack {tensor(encoded(0, b'bad'))})", ["BinaryValue:Fault", "Version"])
    bad_nat = packed(("node", b"\6", [("leaf", b"BV1"), ("node", b"\3", [("leaf", b"\0")])]))
    add(f"(binary-value:unpack {tensor(bad_nat)})", ["Binary:TreeFault", "InvalidNatural"])
    add(f"(binary-value:unpack {tensor(encoded('name') + b'\\0')})", ["Binary:TreeFault", "trailing-data"])
    program += ['!(import! &self (library dag))', '!(import! &self (library syntax))']
    add('''(let $handle (dag:from-forest ())
      (let $result (binary-value:pack $handle) (let $closed (dag:close $handle) $result)))''',
        ["BinaryValue:Fault", "UnsupportedValue"])
    add('''(let $source (syntax:restore (syntax:read "(f $x)")) (binary-value:pack $source))''',
        ["BinaryValue:Fault", "UnsupportedValue"])
    code = "\n".join(program) + "\n"
    check_controls(code)
    path = args.output / "values.metta"
    path.write_text(code)
    run = subprocess.run([str(args.cetta.resolve()), "--lang", "petta", str(path.resolve())],
                         cwd=ROOT, text=True, capture_output=True, timeout=60)
    (args.output / "values.log").write_text(run.stdout + run.stderr)
    actual = [f for f in parse_forms(run.stdout) if isinstance(f, list) and f and f[0] == "ValueCase"]
    indexed = {f[1]: f for f in actual}
    differences = [{"case": i, "expected": want, "actual": indexed.get(str(i))}
                   for i, want in enumerate(expected) if indexed.get(str(i)) != want]
    inert = all(line.strip() not in ("BVALUE-MUST-NOT-RUN", '"BVALUE-MUST-NOT-RUN"')
                for line in run.stdout.splitlines())
    receipt = {"passed": run.returncode == 0 and not differences and len(actual) == len(expected) and inert,
               "checks": len(expected), "answers": len(actual), "mismatches": differences,
               "decoded_execution_absent": inert,
               "scope": "closed protocol-value profile, exact BV1 bytes and inert decoded values; no authority transport"}
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({**receipt, "mismatches": [{"case": d["case"]} for d in differences]}))
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
