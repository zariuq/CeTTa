#!/usr/bin/env python3
"""Reproducibility and ambiguity mutations for the prefix-reader compiler."""

from __future__ import annotations

from pathlib import Path
import tempfile

from compile_gslt_prefix_reader_v1 import CompileError, generate


ROOT = Path(__file__).resolve().parents[1]
SYNTAX = (
    ROOT
    / "experiments/gslt2parse_foundation/presentations/languages/"
    "cetta_prime_reader_v1.metta"
)
CLASSES = (
    ROOT
    / "experiments/gslt2parse_foundation/presentations/shared/"
    "cetta_prime_scalar_classes_v1.metta"
)
PROJECTION = (
    ROOT
    / "experiments/gslt2parse_foundation/presentations/shared/"
    "cetta_prime_atom_projection_v1.metta"
)
CHECKED_C = ROOT / "src/generated/prime_reader_direct_v1.generated.c"
CHECKED_H = ROOT / "src/generated/prime_reader_direct_v1.generated.h"


class GateFailure(RuntimeError):
    pass


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if text.count(old) != 1:
        raise GateFailure(f"{label}: mutation anchor count changed")
    return text.replace(old, new, 1)


def replace_in_rule(
    text: str, rule_name: str, old: str, new: str, label: str
) -> str:
    start = text.find(f"(rule {rule_name}")
    if start < 0:
        raise GateFailure(f"{label}: source rule disappeared")
    end = text.find("\n\n    (rule ", start)
    if end < 0:
        end = len(text)
    rule = text[start:end]
    mutated = replace_once(rule, old, new, label)
    return text[:start] + mutated + text[end:]


def compile_to(
    syntax: Path,
    classes: Path,
    projection: Path,
    directory: Path,
    stem: str,
) -> tuple[bytes, bytes]:
    output_directory = directory / stem
    output_directory.mkdir()
    output_c = output_directory / "prime_reader_direct_v1.generated.c"
    output_h = output_directory / "prime_reader_direct_v1.generated.h"
    generate(
        syntax,
        classes,
        projection,
        "cetta-prime-v1",
        "prime_reader_direct_v1",
        output_c,
        output_h,
    )
    return output_c.read_bytes(), output_h.read_bytes()


def expect_compile_error(
    syntax: Path,
    classes: Path,
    projection: Path,
    directory: Path,
    stem: str,
    needle: str,
) -> None:
    try:
        compile_to(syntax, classes, projection, directory, stem)
    except CompileError as error:
        if needle not in str(error):
            raise GateFailure(
                f"{stem}: expected rejection containing {needle!r}, got {error}"
            ) from error
        return
    raise GateFailure(f"{stem}: semantic mutation unexpectedly compiled")


def main() -> int:
    compiler_source = (ROOT / "tools/compile_gslt_prefix_reader_v1.py").read_text(
        encoding="utf-8"
    )
    if "require_render" in compiler_source:
        raise GateFailure("prefix compiler regressed to rendered-form checking")

    runtime = ROOT / "runtime"
    runtime.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix="gslt-prefix-reader-compiler-v1-", dir=runtime
    ) as temporary:
        directory = Path(temporary)
        baseline_c, baseline_h = compile_to(
            SYNTAX, CLASSES, PROJECTION, directory, "baseline"
        )
        if baseline_c != CHECKED_C.read_bytes() or baseline_h != CHECKED_H.read_bytes():
            raise GateFailure("checked-in Prime reader does not reproduce byte-for-byte")

        projection_change = directory / "projection_change.metta"
        projection_change.write_text(
            replace_once(
                PROJECTION.read_text(encoding="utf-8"),
                "(sexpr-codepoint-map 110 10)",
                "(sexpr-codepoint-map 110 11)",
                "escape action",
            ),
            encoding="utf-8",
        )
        changed_c, _ = compile_to(
            SYNTAX, CLASSES, projection_change, directory, "projection_change"
        )
        if (
            b"{UINT32_C(110), UINT32_C(11)}" not in changed_c
            or b"{UINT32_C(110), UINT32_C(10)}" in changed_c
        ):
            raise GateFailure("projection mutation did not alter generated actions")

        anonymous_policy = directory / "anonymous_policy.metta"
        anonymous_policy.write_text(
            replace_once(
                PROJECTION.read_text(encoding="utf-8"),
                "anonymous-variable empty fresh-anonymous",
                "anonymous-variable empty no-anonymous",
                "anonymous-variable projection",
            ),
            encoding="utf-8",
        )
        expect_compile_error(
            SYNTAX,
            CLASSES,
            anonymous_policy,
            directory,
            "anonymous_policy",
            "fresh anonymous variable",
        )

        plain_overlap = directory / "plain_overlap.metta"
        plain_overlap.write_text(
            replace_in_rule(
                CLASSES.read_text(encoding="utf-8"),
                "member-cetta-prime-token-start-plain-scalar",
                "\n        (different ?codepoint (cp 64))",
                "",
                "ordinary-word/quote overlap",
            ),
            encoding="utf-8",
        )
        expect_compile_error(
            SYNTAX,
            plain_overlap,
            PROJECTION,
            directory,
            "plain_overlap",
            "token dispatch overlaps a prefix alternative",
        )

        prefixed_overlap = directory / "prefixed_overlap.metta"
        prefixed_overlap.write_text(
            replace_in_rule(
                CLASSES.read_text(encoding="utf-8"),
                "member-cetta-prime-token-start-nonat-scalar",
                "\n        (different ?codepoint (cp 64))",
                "",
                "ordinary-variable/structural-variable overlap",
            ),
            encoding="utf-8",
        )
        expect_compile_error(
            SYNTAX,
            prefixed_overlap,
            PROJECTION,
            directory,
            "prefixed_overlap",
            "token dispatch overlaps a prefix alternative",
        )

        structural_overlap = directory / "structural_overlap.metta"
        structural_overlap.write_text(
            replace_once(
                SYNTAX.read_text(encoding="utf-8"),
                "(node quote (right (char (cp 64))",
                "(node quote (right (char (cp 40))",
                "quote/expression overlap",
            ),
            encoding="utf-8",
        )
        expect_compile_error(
            structural_overlap,
            CLASSES,
            PROJECTION,
            directory,
            "structural_overlap",
            "prefix dispatch overlaps a structural scalar",
        )

        duplicate_role = directory / "duplicate_role.metta"
        duplicate_role.write_text(
            replace_once(
                PROJECTION.read_text(encoding="utf-8"),
                "structural-reference resolve-name closed",
                "structural-reference named-variable closed",
                "duplicate prefix meaning",
            ),
            encoding="utf-8",
        )
        expect_compile_error(
            SYNTAX,
            CLASSES,
            duplicate_role,
            directory,
            "duplicate_role",
            "four distinct prefix roles",
        )

    print("(GSLTPrefixReaderCompilerV1Summary 3 5 0)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
