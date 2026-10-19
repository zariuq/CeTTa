#!/usr/bin/env python3
"""Check compact Unicode data, authored classes and source-only C preparation.

The data controls contain no UCD source tree. An independent optional source
conformance audit is provided by test_unicode_data_v1.py.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
from hashlib import sha256
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
from uuid import uuid4

from gslt2parse_schema_v1 import SchemaError, Symbol, parse_sexprs, render
from unicode_presentation_v1 import read_catalog, read_lexical_classes

ROOT = Path(__file__).resolve().parents[1]
COMPONENT = ROOT / "langdef/unicode"
PRESENTATION = "unicode_codepoint_classes16_v1.metta"
LEXICAL = "lexical_classes_v1.metta"
PRODUCTS = ("generated/unicode_ucd16_v1.generated.h", "generated/unicode_ucd16_v1.generated.c")


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def replace_once(source, old, new):
    require(source.count(old) == 1, "Mutation must identify one authored occurrence: " + old)
    return source.replace(old, new)


class Gate:
    def __init__(self, output, compiler):
        self.output, self.compiler = output, compiler
        output.mkdir(parents=True, exist_ok=False)
        self.commands, self.rejections = [], []

    def run(self, command, label, success=True):
        result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
        (self.output / (label + ".log")).write_text(result.stdout + result.stderr, encoding="utf-8")
        self.commands.append({"label": label, "argv": command, "returncode": result.returncode})
        require((result.returncode == 0) == success,
                f"{label}: unexpected exit {result.returncode}: {result.stdout}{result.stderr}")
        return result

    def component(self, name, source, lexical):
        tree = self.output / name
        component = tree / "langdef/unicode"
        component.mkdir(parents=True)
        shutil.copy2(COMPONENT / "LICENSE.txt", component / "LICENSE.txt")
        (component / PRESENTATION).write_text(source, encoding="utf-8")
        (component / LEXICAL).write_text(lexical, encoding="utf-8")
        # Compile a local provider copy: its project-relative generated include
        # must select this component's header, not the live tree's product.
        (tree / "native").mkdir()
        for name in ("unicode_class_v1.h", "unicode_class_v1.c", "unicode_class_equality_v1.c"):
            shutil.copy2(ROOT / "native" / name, tree / "native" / name)
        require(not (component / "ucd").exists(), "Source-only package includes UCD")
        return component

    def generate(self, component, label):
        self.run([sys.executable, str(ROOT / "tools/compile_unicode_data_v1.py"),
                  "--root", str(component)], label)

    def native(self, component, label):
        tree = component.parent.parent
        binary = tree / "unicode-client-control"
        self.run(self.compiler + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                  "-I" + str(tree), "-I" + str(ROOT), "-I" + str(ROOT / "src"),
                  str(tree / "native/unicode_class_v1.c"),
                  str(tree / "native/unicode_class_equality_v1.c"),
                  str(component / PRODUCTS[1]),
                  str(ROOT / "tests/support/test_unicode_class_v1.c"), "-o", str(binary)], label)
        return binary

    def reject(self, label, source, lexical, lexical_error=False, diagnostic=None):
        component = self.component(label, source, lexical)
        try:
            catalog = read_catalog(component / PRESENTATION)
            if lexical_error:
                read_lexical_classes(component / LEXICAL, catalog)
        except (SchemaError, ValueError) as error:
            if diagnostic:
                require(diagnostic in str(error), f"{label}: wrong diagnostic: {error}")
            (self.output / (label + ".log")).write_text(str(error) + "\n", encoding="utf-8")
            self.rejections.append(label)
        else:
            raise AssertionError("Malformed input accepted: " + label)


def changed_field(source, field, transform):
    root, = parse_sexprs(source)
    output = []
    found = 0
    for item in root[1:]:
        if item[0] == Symbol(field):
            output.append((item[0], *transform(item[1:])))
            found += 1
        else:
            output.append(item)
    require(found == 1, "Missing or duplicate source field: " + field)
    return render((root[0], *output)) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    output = args.output or ROOT / "runtime/unicode-presentation-v1" / (
        datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid4().hex[:8])
    gate = Gate(output, shlex.split(args.cc))
    source = (COMPONENT / PRESENTATION).read_text(encoding="utf-8")
    lexical = (COMPONENT / LEXICAL).read_text(encoding="utf-8")
    source_only = gate.component("source-only", source, lexical)
    gate.generate(source_only, "source-only-generate")
    for name in PRODUCTS:
        require((source_only / name).read_bytes() == (COMPONENT / name).read_bytes(),
                "Source-only preparation differs: " + name)
    binary = gate.native(source_only, "source-only-native-build")
    gate.run([str(binary)], "source-only-native-run")

    equivalent_lexical = replace_once(lexical,
        '(lexical-class-v1 AsciiHex Scalars (property Unicode16 "AHex" "Y"))',
        '(lexical-class-v1 AsciiHex Scalars '
        '(union (range (cp 97) (cp 102)) '
        '(union (range (cp 65) (cp 70)) '
        '(union (range (cp 48) (cp 52)) '
        '(union (range (cp 51) (cp 57)) (range (cp 48) (cp 57)))))))')
    equivalent = gate.component("equivalent-class", source, equivalent_lexical)
    gate.generate(equivalent, "equivalent-class-generate")
    for name in PRODUCTS:
        require((equivalent / name).read_bytes() == (source_only / name).read_bytes(),
                "Equivalent class expressions change native values: " + name)

    gate.reject("missing-universe", changed_field(source, "domains", lambda rows: rows[1:]), lexical)
    gate.reject("bad-list-reference", changed_field(source, "classes",
                lambda rows: ((*rows[0][:2], 1000000), *rows[1:])), lexical)
    gate.reject("duplicate-class", changed_field(source, "classes",
                lambda rows: (*rows, rows[0])), lexical)
    gate.reject("overlapping-decimal-blocks", changed_field(source, "decimal-zeroes",
                lambda rows: (*rows[:1], rows[0] + 1, *rows[1:])), lexical)
    gate.reject("decimal-coverage", changed_field(source, "decimal-zeroes",
                lambda rows: (rows[0] + 1, *rows[1:])), lexical)
    gate.reject("surrogate-fold", changed_field(source, "fold-groups",
                lambda rows: ((55296, 55297), *rows[1:])), lexical)
    gate.reject("duplicate-property-alias", changed_field(source, "property-aliases",
                lambda rows: (rows[0], *rows)), lexical)
    gate.reject("wrong-property-version", source, replace_once(lexical,
                '(intersection (property Unicode16 "gc" "Nd") (range (cp 0) (cp 127)))',
                '(intersection (property Unicode17 "gc" "Nd") (range (cp 0) (cp 127)))'), True)
    gate.reject("unknown-property", source, replace_once(lexical,
                '(intersection (property Unicode16 "gc" "Nd") (range (cp 0) (cp 127)))',
                '(intersection (property Unicode16 "gc" "Absent") (range (cp 0) (cp 127)))'), True)
    gate.reject("digit-value-coverage", source, replace_once(lexical,
                '(digit-values-v1 AsciiHex (cp 65) (cp 70) 10)',
                '(digit-values-v1 AsciiHex (cp 65) (cp 69) 10)'), True)
    gate.reject("scalar-surrogate", source, replace_once(lexical,
                '(lexical-class-v1 UnicodeScalar Scalars (domain Scalars))',
                '(lexical-class-v1 UnicodeScalar Scalars (domain CodePoints))'), True)
    gate.reject("false-hex-equality", source, replace_once(lexical,
                '(range (cp 97) (cp 102))', '(range (cp 97) (cp 101))'), True,
                "first different value 0x0066")
    gate.reject("false-scalar-equality", source, replace_once(lexical,
                '(range (cp 55296) (cp 57343))', '(range (cp 55295) (cp 57343))'), True,
                "first different value 0xd7ff")

    mutant_lexical = replace_once(lexical,
        '(digit-values-v1 AsciiHex (cp 65) (cp 70) 10)',
        '(digit-values-v1 AsciiHex (cp 65) (cp 70) 11)')
    mutant = gate.component("hex-value-mutant", source, mutant_lexical)
    gate.generate(mutant, "hex-value-mutant-generate")
    require((mutant / PRODUCTS[1]).read_bytes() != (source_only / PRODUCTS[1]).read_bytes(),
            "Changed declared value does not reach generated code")
    mutant_binary = gate.native(mutant, "hex-value-mutant-native-build")
    failed = gate.run([str(mutant_binary)], "hex-value-mutant-run", success=False)
    require("FAIL" in failed.stderr, "Native oracle did not detect altered values")
    receipt = {"format": "unicode-presentation-control-v1", "status": "passed",
               "presentation_sha256": sha256(source.encode()).hexdigest(),
               "lexical_sha256": sha256(lexical.encode()).hexdigest(),
               "products": {name: sha256((source_only / name).read_bytes()).hexdigest() for name in PRODUCTS},
               "rejections": gate.rejections, "commands": gate.commands,
               "native_mutation_detected": True, "source_only": True,
               "equivalent_expressions_same_native_value": True}
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"PASS: compact shared Unicode values, source-only C preparation, "
          f"{len(gate.rejections)} rejection controls and native semantic mutation detection")


if __name__ == "__main__":
    main()
