#!/usr/bin/env python3
"""Lower authored postings observation descriptors into checked tables."""

import argparse
import hashlib
from pathlib import Path
import re
import subprocess

from generate_tensor_descriptors import linked, nullary, record
from gslt2parse_schema_v1 import StringLiteral, Symbol, parse_sexprs
from library_spelling import spelling

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "langdef/postings/Descriptors.module"
HEADER = ROOT / "src/generated/postings_descriptors.generated.h"
LEAN = ROOT / "langdef/postings/generated/Descriptors.lean"
METTA = ROOT / "lib/postings_descriptors.metta"
PETTA = ROOT / "lib/petta/postings_descriptors.metta"

WIRES = {
    "before", "all", "tensor-before", "tensor-all", "table-before",
    "table-all", "table-tensor-before", "table-tensor-all", "stats",
}

PUBLIC_KEYS = {
    "Before": "before",
    "All": "all",
    "TensorBefore": "tensor-before",
    "TensorAll": "tensor-all",
    "TableBefore": "table-before",
    "TableAll": "table-all",
    "TableTensorBefore": "table-tensor-before",
    "TableTensorAll": "table-tensor-all",
    "Stats": "stats",
    "PrepareIndex": "prepare-index",
    "PrepareTable": "prepare-table",
    "PrepareSpaceTableAttempt": "prepare-space-table-attempt",
    "Inspect": "inspect",
    "Counts": "counts",
    "SourcesCurrent": "sources-current",
    "Close": "close",
}


def descriptor_pattern(value):
    name, arguments = record(value, "PApp", 2)
    if (not isinstance(name, StringLiteral) or
            not re.fullmatch(r"[A-Za-z][A-Za-z0-9-]*", name.text)):
        raise ValueError("descriptor names must be identifiers or wire names")
    return name.text, [descriptor_pattern(argument)
                       for argument in linked(arguments)]


def tables(binary: Path):
    run = subprocess.run(
        [str(binary), "--no-results", "--lang", "petta@1.0.6", "--profile",
         "extended", "tests/langdef/postings/descriptors_runtime_v1.metta"],
        cwd=ROOT, text=True, capture_output=True, check=True,
    )
    values = parse_sexprs(run.stdout)
    if len(values) != 1:
        raise ValueError("module reader did not return one document")
    _, name, imports, theories, _, _, _ = record(values[0], "ModuleValueV1", 7)
    if (name != StringLiteral("PostingsDescriptors") or
            imports != Symbol("LNil") or len(linked(theories)) != 1):
        raise ValueError("unexpected descriptor module structure")
    _, parameters, body = record(linked(theories)[0], "ModuleTheoryV1", 3)
    base, equations = record(body, "ModuleAddEquationsV1", 2)
    term_spellings = {}
    while isinstance(base, tuple) and base[0] in (
            Symbol("ModuleAddTypesV1"), Symbol("ModuleAddTermsV1")):
        if base[0] == Symbol("ModuleAddTermsV1"):
            base, declarations = record(base, "ModuleAddTermsV1", 2)
            for declaration in linked(declarations):
                _, term = record(declaration, "ModuleDeclarationV1", 2)
                rule, = record(term, "ModuleCoreTermV1", 1)
                label, _, parameters_, syntax, _, _ = record(
                    rule, "GrammarRuleV2", 6)
                terminals = linked(syntax)
                if (not isinstance(label, StringLiteral) or
                        parameters_ != Symbol("LNil") or len(terminals) != 1):
                    continue
                spelling, = record(terminals[0], "Terminal", 1)
                if isinstance(spelling, StringLiteral):
                    term_spellings[label.text] = spelling.text
        else:
            base, _ = record(base, "ModuleAddTypesV1", 2)
    if parameters != Symbol("LNil") or base != Symbol("ModuleEmptyV1"):
        raise ValueError("descriptor module must be a closed table")

    modes, observations, seen = [], [], set()
    for declaration in linked(equations):
        _, equation = record(declaration, "ModuleDeclarationV1", 2)
        _, context, left, right = record(equation, "ModuleEquationV1", 4)
        if context != Symbol("LNil"):
            raise ValueError("descriptor equations must be closed")
        kind, key_arguments = descriptor_pattern(left)
        if len(key_arguments) != 1:
            raise ValueError("expected one descriptor key")
        key = nullary(key_arguments[0])
        if (kind, key) in seen:
            raise ValueError("duplicate descriptor")
        seen.add((kind, key))
        target, arguments = descriptor_pattern(right)
        if kind == "PostingsCountMode" and target == "CountMode" and len(arguments) == 4:
            wire_name = nullary(arguments[0])
            wire = term_spellings.get(wire_name)
            if wire not in WIRES:
                raise ValueError("unknown count-mode wire spelling")
            modes.append((key, wire_name, wire,
                          *(nullary(arg) for arg in arguments[1:])))
        elif kind == "PostingsObservation" and target == "Observe":
            observations.append((key, *(nullary(arg) for arg in arguments)))
        else:
            raise ValueError("unsupported descriptor equation")
    if {wire for _, _, wire, *_ in modes} != WIRES:
        raise ValueError("count-mode table must define every native wire mode exactly once")
    if not observations:
        raise ValueError("observation table must be nonempty")
    return modes, observations


def generated(binary: Path):
    modes, observations = tables(binary)
    digest = hashlib.sha256(SOURCE.read_bytes()).hexdigest()
    header = [
        "/* Generated from the shared .module reader; do not edit. */",
        "#ifndef CETTA_POSTINGS_DESCRIPTORS_GENERATED_H",
        "#define CETTA_POSTINGS_DESCRIPTORS_GENERATED_H",
        f'#define CETTA_POSTINGS_DESCRIPTORS_SHA256 "{digest}"',
        "#define CETTA_POSTINGS_COUNT_MODE_ROWS(X) \\",
    ]
    for index, (key, wire_name, wire, family, extent, result) in enumerate(modes):
        suffix = " \\" if index + 1 < len(modes) else ""
        header.append(
            f'    X({key.upper()}, "{wire}", {family.upper()}, '
            f'{extent.upper()}, {result.upper()}){suffix}'
        )
    header.append("#define CETTA_POSTINGS_OBSERVATION_ROWS(X) \\")
    for index, (key, *policies) in enumerate(observations):
        suffix = " \\" if index + 1 < len(observations) else ""
        header.append(f'    X("{PUBLIC_KEYS[key]}", "{"|".join(policies)}"){suffix}')
    header.extend(["#endif", ""])

    lean = [
        "import Mettapedia.GSLT.LanguageDef.ModuleSource.Commands",
        "",
        "open Mettapedia.GSLT.LanguageDef.ModuleSource.Commands",
        "",
        "set_option maxRecDepth 4096",
        "set_option maxHeartbeats 800000",
        "",
        '#module_file "../Descriptors.module"',
        "",
        "namespace CeTTaPostingsDescriptors",
        "open Mettapedia.OSLF.MeTTaIL.Syntax",
        "",
        "private def language : LanguageDef :=",
        '  PostingsDescriptors.language.toOption.getD (LanguageDef.empty "invalid")',
        "",
        "theorem source_loaded : PostingsDescriptors.language = .ok language := rfl",
        "",
        "def countModeRows : List (String × String × String × String × String × String) := [",
    ]
    lean += [
        f'  ("{key}", "{wire_name}", "{wire}", "{family}", "{extent}", "{result}")' +
        ("," if index + 1 < len(modes) else "")
        for index, (key, wire_name, wire, family, extent, result) in enumerate(modes)
    ]
    lean += [
        "]", "",
        "def sourceCountModeRows : List (String × Pattern) :=",
        "  language.equations.filterMap fun equation =>",
        "    match equation.left with",
        '    | Pattern.apply "PostingsCountMode" [Pattern.apply label []] =>',
        "        some (label, equation.right)",
        "    | _ => none", "",
        "def encodedCountModeRows : List (String × Pattern) :=",
        "  countModeRows.map fun (label, wireName, _wire, family, extent, result) =>",
        '    (label, Pattern.apply "CountMode" [Pattern.apply wireName [],',
        "      Pattern.apply family [], Pattern.apply extent [], Pattern.apply result []])",
        "",
        "theorem count_mode_rows_match_source :",
        "    sourceCountModeRows = encodedCountModeRows := by",
        "  decide +kernel", "",
        "#print axioms count_mode_rows_match_source", "",
        "def observationRows : List (String × List String) := [",
    ]
    lean += [
        f'  ("{key}", [{", ".join(chr(34) + policy + chr(34) for policy in policies)}])' +
        ("," if index + 1 < len(observations) else "")
        for index, (key, *policies) in enumerate(observations)
    ]
    lean += [
        "]", "",
        "def sourceObservationRows : List (String × Pattern) :=",
        "  language.equations.filterMap fun equation =>",
        "    match equation.left with",
        '    | Pattern.apply "PostingsObservation" [Pattern.apply label []] =>',
        "        some (label, equation.right)",
        "    | _ => none", "",
        "def encodedObservationRows : List (String × Pattern) :=",
        "  observationRows.map fun (label, policies) =>",
        '    (label, Pattern.apply "Observe"',
        "      (policies.map fun policy => Pattern.apply policy []))", "",
        "theorem observation_rows_match_source :",
        "    sourceObservationRows = encodedObservationRows := by",
        "  decide +kernel", "",
        "#print axioms observation_rows_match_source", "",
        "end CeTTaPostingsDescriptors", "",
    ]

    metta = [
        "; Generated from PostingsDescriptors; do not edit.",
        "(: postings:count-mode-descriptor (-> Atom Atom))",
        "(: postings:observation-descriptor (-> Atom Atom))",
    ]
    for key, wire_name, wire, family, extent, result in modes:
        metta.append(
            f'(= (postings:count-mode-descriptor {PUBLIC_KEYS[key]}) '
            f'(PostingsCountMode {wire} {family} {extent} {result}))'
        )
    for key, *policies in observations:
        metta.append(
            f'(= (postings:observation-descriptor {PUBLIC_KEYS[key]}) '
            f'(PostingsPolicy {PUBLIC_KEYS[key]} ({" ".join(policies)})))'
        )
    metta_text = "\n".join(metta) + "\n"
    return "\n".join(header), "\n".join(lean), metta_text, spelling(metta_text, "petta")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cetta", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    outputs = generated(args.cetta.resolve())
    for path, value in zip((HEADER, LEAN, METTA, PETTA), outputs):
        if args.check:
            if not path.is_file() or path.read_text() != value:
                raise SystemExit(
                    f"postings descriptor output differs: {path.relative_to(ROOT)}"
                )
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(value)
    print("PostingsDescriptors 4 0")


if __name__ == "__main__":
    main()
