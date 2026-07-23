#!/usr/bin/env python3
"""Reproducibility and semantic-mutation gate for direct GSLT readers."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile

from compile_gslt_direct_reader_v1 import (
    CompileError,
    generate,
    normalize_syntax_ir,
)
from gslt2parse_schema_v1 import Symbol, parse_presentation, render


ROOT = Path(__file__).resolve().parents[1]
SYNTAX = (
    ROOT
    / "experiments/gslt2parse_foundation/presentations/languages/he_reader_v1.metta"
)
CLASSES = (
    ROOT
    / "experiments/gslt2parse_foundation/presentations/shared/he_reader_scalar_classes_v1.metta"
)
PROJECTION = (
    ROOT
    / "experiments/gslt2parse_foundation/presentations/shared/cetta_he_atom_projection_v1.metta"
)
CHECKED_C = ROOT / "src/generated/he_reader_direct_v1.generated.c"
CHECKED_H = ROOT / "src/generated/he_reader_direct_v1.generated.h"


class GateFailure(RuntimeError):
    pass


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if text.count(old) != 1:
        raise GateFailure(f"{label}: mutation anchor count changed")
    return text.replace(old, new, 1)


def compile_to(
    syntax: Path, classes: Path, projection: Path, directory: Path, stem: str
) -> tuple[bytes, bytes]:
    output_directory = directory / stem
    output_directory.mkdir()
    output_c = output_directory / "he_reader_direct_v1.generated.c"
    output_h = output_directory / "he_reader_direct_v1.generated.h"
    generate(
        syntax,
        classes,
        projection,
        "cetta-he-v1",
        "he_reader_direct_v1",
        output_c,
        output_h,
    )
    return output_c.read_bytes(), output_h.read_bytes()


def ascii_class(c_source: bytes, class_name: str) -> tuple[int, ...]:
    text = c_source.decode("utf-8")
    match = re.search(
        rf"static const uint8_t [A-Za-z0-9_]+_{re.escape(class_name)}_ascii\[\] = \{{\n"
        rf"(?P<body>.*?)\n\}};",
        text,
        flags=re.DOTALL,
    )
    if match is None:
        raise GateFailure(f"generated class {class_name} is absent")
    values = tuple(int(item) for item in re.findall(r"\b[01]\b", match.group("body")))
    if len(values) != 256:
        raise GateFailure(f"generated class {class_name} has {len(values)} ASCII rows")
    return values


def check_syntax_ir_agreement(chart_binary: Path) -> None:
    presentations = ROOT / "experiments/gslt2parse_foundation/presentations"
    sources = [
        presentations / "parserpack/parser_pack_core_v1.metta",
        presentations / "reflection/finite_horn_reflection_v1.metta",
        presentations / "core/syntax_core_v1.metta",
        presentations / "shared/lookahead_core_v1.metta",
        presentations / "compiler/syntax_compiler_v1.metta",
        SYNTAX,
    ]
    completed = subprocess.run(
        [
            str(chart_binary),
            *(str(source) for source in sources),
            "--query-text",
            "(compile-definition ?name ?result)",
            "--timeout",
            "30",
        ],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        timeout=60,
    )
    if completed.returncode != 0:
        raise GateFailure(
            "SyntaxCompilerV1 execution failed: " + completed.stderr.strip()
        )
    try:
        result = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        raise GateFailure("SyntaxCompilerV1 emitted malformed JSON") from error
    syntax_ir = normalize_syntax_ir(parse_presentation(SYNTAX))
    expected = {
        render(
            (
                Symbol("compile-definition"),
                Symbol(name),
                (Symbol("sir-definition"), Symbol(name), grammar),
            )
        )
        for name, grammar in syntax_ir.definitions.items()
    }
    observed = set(result.get("terms", []))
    if result.get("outcome") != "Ambiguous" or observed != expected:
        missing = sorted(expected - observed)
        extra = sorted(observed - expected)
        raise GateFailure(
            "native and structural SIR normalization disagree; "
            f"missing={missing[:2]} extra={extra[:2]}"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--chart-binary", required=True, type=Path)
    arguments = parser.parse_args()
    runtime = ROOT / "runtime"
    runtime.mkdir(exist_ok=True)
    compiler_source = (ROOT / "tools/compile_gslt_direct_reader_v1.py").read_text(
        encoding="utf-8"
    )
    if "require_render" in compiler_source:
        raise GateFailure("direct compiler regressed to fixed rendered-form checking")
    check_syntax_ir_agreement(arguments.chart_binary)

    with tempfile.TemporaryDirectory(
        prefix="gslt-direct-reader-compiler-v1-", dir=runtime
    ) as temporary:
        directory = Path(temporary)
        baseline_c, baseline_h = compile_to(
            SYNTAX, CLASSES, PROJECTION, directory, "baseline"
        )
        if baseline_c != CHECKED_C.read_bytes() or baseline_h != CHECKED_H.read_bytes():
            raise GateFailure("checked-in direct reader does not reproduce byte-for-byte")
        baseline_word_start = ascii_class(baseline_c, "he_word_start_scalar")
        if baseline_word_start[ord("#")] != 1:
            raise GateFailure("ratified word-start table does not admit #")

        mutant_classes = directory / "mutant_classes.metta"
        mutant_classes.write_text(
            replace_once(
                CLASSES.read_text(encoding="utf-8"),
                "(different ?codepoint (cp 34)) (different ?codepoint (cp 36))",
                "(different ?codepoint (cp 34)) (different ?codepoint (cp 35)) "
                "(different ?codepoint (cp 36))",
                "word-start # exclusion",
            ),
            encoding="utf-8",
        )
        class_mutant_c, _ = compile_to(
            SYNTAX, mutant_classes, PROJECTION, directory, "class_mutant"
        )
        if ascii_class(class_mutant_c, "he_word_start_scalar")[ord("#")] != 0:
            raise GateFailure("word-start mutation did not alter generated recognition data")

        mutant_projection = directory / "mutant_projection.metta"
        mutant_projection.write_text(
            replace_once(
                PROJECTION.read_text(encoding="utf-8"),
                "(sexpr-codepoint-map 110 10)",
                "(sexpr-codepoint-map 110 11)",
                "newline projection",
            ),
            encoding="utf-8",
        )
        projection_mutant_c, _ = compile_to(
            SYNTAX, CLASSES, mutant_projection, directory, "projection_mutant"
        )
        if (
            b"{UINT32_C(110), UINT32_C(11)}" not in projection_mutant_c
            or b"{UINT32_C(110), UINT32_C(10)}" in projection_mutant_c
        ):
            raise GateFailure("projection mutation did not alter generated action data")

        mutant_syntax = directory / "mutant_syntax.metta"
        mutant_syntax.write_text(
            replace_once(
                SYNTAX.read_text(encoding="utf-8"),
                "(node expression (right (char (cp 40))",
                "(node expression (right (char (cp 91))",
                "expression-open delimiter",
            ),
            encoding="utf-8",
        )
        try:
            compile_to(mutant_syntax, CLASSES, PROJECTION, directory, "syntax_mutant")
        except CompileError:
            pass
        else:
            raise GateFailure("overlapping structural mutation did not fail closed")

    print("(GSLTDirectReaderCompilerV1Summary 2 3 0)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
