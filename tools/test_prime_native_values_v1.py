#!/usr/bin/env python3
"""Implicit field demand at Prime's native-library value boundary."""

import argparse
from pathlib import Path

from test_parser_artifacts_v1 import headed, run
from test_lbnf_values_v1 import scalar_text
from gslt2parse_schema_v1 import Symbol

ROOT = Path(__file__).resolve().parents[1]

DEFINITIONS = """!(import! &self json)
(: native-field:number (-> %Undefined% Atom))
(: native-field:pair (-> %Undefined% Atom))
(: native-field:two (-> %Undefined% %Undefined% Atom))
(= (native-field:digits) "7")
(= (native-field:pick) (superpose ("7" "8")))
(= (native-field:number $x) (JsonNumberV1 $x))
(= (native-field:pair $x)
   (JsonArrayV1 ((JsonNumberV1 $x) (JsonNumberV1 $x))))
(= (native-field:two $x $y)
   (JsonArrayV1 ((JsonNumberV1 $x) (JsonNumberV1 $y))))
"""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--output", type=Path,
                        default=ROOT / "runtime/prime-native-values-public")
    args = parser.parse_args()
    binary, output = args.cetta.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    checks = 0

    for language in ("he", "petta", "prime"):
        source = DEFINITIONS + """
!(json:inspect-stringify (JsonNumberV1 "7"))
!(json:inspect-stringify (native-field:number (native-field:digits)))
!(let $digits "7"
   (json:inspect-stringify (native-field:number $digits)))
"""
        values = run(binary, language, output, "computed", source)
        if [scalar_text(value) for value in values[-3:]] != ["7", "7", "7"]:
            raise AssertionError((language, "computed fields", values))
        checks += 3

    source = DEFINITIONS + """
!(json:inspect-stringify (native-field:pair (native-field:pick)))
!(json:inspect-stringify (native-field:two (native-field:pick) (native-field:pick)))
!(json:inspect-stringify (native-field:number (superpose ("7" "7"))))
"""
    values = run(binary, "prime", output, "occurrences", source)
    values = [value for value in values if value != Symbol(",")]
    expected = ["[7,7]", "[8,8]", "[7,7]", "[7,8]", "[8,7]", "[8,8]", "7", "7"]
    if [scalar_text(value) for value in values[-8:]] != expected:
        raise AssertionError(("branch-local sharing and multiplicity", values))
    checks += 8

    effects = """
!(bind! &native-field:count (new-state 0))
(= (native-field:effect)
   (let $old (get-state &native-field:count)
     (let $_ (change-state! &native-field:count (+ $old 1)) "7")))
"""
    source = DEFINITIONS + effects + """
!(let $rendered (json:inspect-stringify (native-field:pair (native-field:effect)))
   (let $count (get-state &native-field:count)
     (function (return (NativeFieldEffectV1 $rendered $count)))))
"""
    values = run(binary, "prime", output, "single-effect", source)
    records = [value for value in values if headed(value, "NativeFieldEffectV1", 2)]
    if len(records) != 1 or scalar_text(records[0][1]) != "[7,7]" or records[0][2] != 1:
        raise AssertionError(("single effect retained across native return", values))
    checks += 2

    for name, barrier in (("quote", "quote"), ("delay", "delay")):
        source = DEFINITIONS + effects + f"""
(: native-field:held (-> %Undefined% Atom))
(= (native-field:held $x) (JsonNumberV1 ({barrier} $x)))
!(let $rendered (json:inspect-stringify (native-field:held (native-field:effect)))
   (let $count (get-state &native-field:count)
     (function (return (NativeFieldHeldV1 $rendered $count)))))
"""
        values = run(binary, "prime", output, name, source)
        records = [value for value in values if headed(value, "NativeFieldHeldV1", 2)]
        if len(records) != 1 or records[0][2] != 0 or not headed(records[0][1], "JsonFailureV1", 4):
            raise AssertionError((name, "explicitly held field must not execute", values))
        checks += 2

    print(f"(PrimeNativeValuesPublicV1Summary {checks} 0)")


if __name__ == "__main__":
    main()
