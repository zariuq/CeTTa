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
    "table-all", "table-tensor-before", "table-tensor-all", "table-fold-before",
    "table-fold-columns-before", "stats",
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
    "TableFoldBefore": "table-fold-before",
    "TableFoldColumnsBefore": "table-fold-columns-before",
    "FoldTable": "fold-table",
    "FoldColumns": "fold-columns",
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


def read_descriptor_module(binary: Path, runtime: str, expected_name: str):
    run = subprocess.run(
        [str(binary), "--no-results", "--lang", "petta@1.0.6", "--profile",
         "extended", runtime],
        cwd=ROOT, text=True, capture_output=True, check=True,
    )
    values = parse_sexprs(run.stdout)
    if len(values) != 1:
        raise ValueError("module reader did not return one document")
    _, name, imports, theories, _, _, _ = record(values[0], "ModuleValueV1", 7)
    if (name != StringLiteral(expected_name) or
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

    return term_spellings, linked(equations)


def tables(binary: Path):
    term_spellings, equations = read_descriptor_module(
        binary, "tests/langdef/postings/descriptors_runtime_v1.metta", "PostingsDescriptors")

    modes, observations, projections, column_types, seen = [], [], [], [], set()
    for declaration in equations:
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
        elif kind == "PostingsProjection" and target == "Projection" and arguments:
            wire_name = nullary(arguments[0])
            wire = term_spellings.get(wire_name)
            fields = [nullary(arg) for arg in arguments[1:]]
            schemas = {
                "BlockLayout": ("Postings:BlockLayout", {
                    "RowHead", "RowWidth", "KeysField", "GroupField",
                    "PositionField", "PrefixKeys", "ForbiddenHeads"}),
                "ColumnsLayout": ("Postings:Columns", {"LabelsColumn", "ValueColumns"}),
            }
            if (key not in schemas or wire != schemas[key][0] or
                    len(fields) != len(set(fields)) or set(fields) != schemas[key][1]):
                raise ValueError("invalid projection schema")
            projections.append((key, wire_name, wire, fields))
        elif kind == "PostingsColumnDType" and target == "ColumnDType" and len(arguments) == 2:
            wire_name, leaf = map(nullary, arguments)
            wire = term_spellings.get(wire_name)
            admitted = {"I64": ("i64", "IntegerLeaf"), "F64": ("f64", "FloatingLeaf")}
            if key not in admitted or (wire, leaf) != admitted[key]:
                raise ValueError("unsupported exact column leaf type")
            column_types.append((key, wire_name, wire, leaf))
        else:
            raise ValueError("unsupported descriptor equation")
    if {wire for _, _, wire, *_ in modes} != WIRES:
        raise ValueError("count-mode table must define every native wire mode exactly once")
    if not observations:
        raise ValueError("observation table must be nonempty")
    if {key for key, *_ in projections} != {"BlockLayout", "ColumnsLayout"}:
        raise ValueError("block and columns projection schemas are required")
    if {key for key, *_ in column_types} != {"I64", "F64"}:
        raise ValueError("integer and floating column leaf types are required")
    return modes, observations, projections, column_types


def generated(binary: Path):
    modes, observations, projections, column_types = tables(binary)
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
    _, projection_wire_name, projection_wire, fields = next(
        row for row in projections if row[0] == "BlockLayout")
    header += [
        f'#define CETTA_POSTINGS_BLOCK_LAYOUT_HEAD "{projection_wire}"',
        f'#define CETTA_POSTINGS_BLOCK_LAYOUT_LENGTH {len(fields) + 1}u',
    ]
    for index, field in enumerate(fields, 1):
        header.append(f'#define CETTA_POSTINGS_BLOCK_{field.upper()} {index}u')
    _, column_wire_name, column_wire, column_fields = next(
        row for row in projections if row[0] == "ColumnsLayout")
    header += [
        f'#define CETTA_POSTINGS_COLUMNS_HEAD "{column_wire}"',
        f'#define CETTA_POSTINGS_COLUMNS_LENGTH {len(column_fields) + 1}u',
    ]
    for index, field in enumerate(column_fields, 1):
        header.append(f'#define CETTA_POSTINGS_COLUMNS_{field.upper()} {index}u')
    header.append("#define CETTA_POSTINGS_COLUMN_DTYPE_ROWS(X) \\")
    for index, (key, _wire_name, wire, leaf) in enumerate(column_types):
        suffix = " \\" if index + 1 < len(column_types) else ""
        header.append(f'    X({key}, "{wire}", {str(leaf == "FloatingLeaf").lower()}){suffix}')
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
    ]
    lean += [
        "def blockProjectionFields : List String := [" +
        ", ".join(f'\"{field}\"' for field in fields) + "]", "",
        "def sourceBlockProjection : List Pattern :=",
        "  language.equations.filterMap fun equation =>",
        "    match equation.left with",
        '    | Pattern.apply "PostingsProjection" [Pattern.apply "BlockLayout" []] =>',
        "        some equation.right",
        "    | _ => none", "",
        "theorem block_projection_fields_match_source :",
        '    sourceBlockProjection = [Pattern.apply "Projection"',
        f'      (Pattern.apply "{projection_wire_name}" [] ::',
        "        blockProjectionFields.map (fun field => Pattern.apply field []))] := by",
        "  decide +kernel", "",
        "#print axioms block_projection_fields_match_source", "",
    ]
    lean += [
        "def columnsLayoutFields : List String := [" +
        ", ".join(f'\"{field}\"' for field in column_fields) + "]", "",
    ]
    lean += [
        "def columnTypeRows : List (String × String × String × String) := [",
        *(f'  ("{key}", "{wire_name}", "{wire}", "{leaf}")' +
          ("," if index + 1 < len(column_types) else "")
          for index, (key, wire_name, wire, leaf) in enumerate(column_types)),
        "]", "",
        "def sourceColumnTypeRows : List (String × Pattern) :=",
        "  language.equations.filterMap fun equation =>",
        "    match equation.left with",
        '    | Pattern.apply "PostingsColumnDType" [Pattern.apply label []] =>',
        "        some (label, equation.right)",
        "    | _ => none", "",
        "theorem column_types_match_source :",
        "    sourceColumnTypeRows = columnTypeRows.map (fun (label, wireName, _wire, leaf) =>",
        '      (label, Pattern.apply "ColumnDType" [Pattern.apply wireName [], Pattern.apply leaf []])) := by',
        "  decide +kernel", "",
        "def sourceColumnsProjection : List Pattern :=",
        "  language.equations.filterMap fun equation =>",
        "    match equation.left with",
        '    | Pattern.apply "PostingsProjection" [Pattern.apply "ColumnsLayout" []] =>',
        "        some equation.right",
        "    | _ => none", "",
        "theorem columns_projection_fields_match_source :",
        '    sourceColumnsProjection = [Pattern.apply "Projection"',
        f'      (Pattern.apply "{column_wire_name}" [] ::',
        "        columnsLayoutFields.map (fun field => Pattern.apply field []))] := by",
        "  decide +kernel", "",
        "#print axioms column_types_match_source",
        "#print axioms columns_projection_fields_match_source", "",
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
    variables = {"LabelsColumn": "$labels", "ValueColumns": "$columns"}
    pattern = f'({column_wire} {" ".join(variables[field] for field in column_fields)})'
    first, second = (variables[field] for field in column_fields)
    metta += [
        '(: postings:column-result (-> %Undefined% %Undefined% %Undefined%))',
        '(: postings:column-labels (-> %Undefined% %Undefined%))',
        '(: postings:column-values (-> %Undefined% %Undefined%))',
        '(= (postings:column-result $labels $columns) '
        f'(let* (($last (cons-atom {second} ())) ($tail (cons-atom {first} $last)) '
        f'($record (cons-atom {column_wire} $tail))) (__cetta_lib_list_to_expression $record)))',
        f'(= (postings:column-labels {pattern}) $labels)',
        f'(= (postings:column-values {pattern}) $columns)',
    ]
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
