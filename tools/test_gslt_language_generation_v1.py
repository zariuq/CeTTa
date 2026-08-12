#!/usr/bin/env python3
"""Check deterministic, digest-closed GSLT language-pack generation."""

from __future__ import annotations

import argparse
from dataclasses import replace
from hashlib import sha256
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

import generate_gslt_language_v1 as language_generator
import gslt2parse_schema_v1 as sx


class GateFailure(RuntimeError):
    pass


def check_compiled_plan_wire_canary() -> None:
    presentation = sx.Presentation(
        name="CompiledPlanWireCanaryV1",
        operators=(sx.OperatorDecl("ready", 0),),
        rules=(
            sx.RuleDecl(
                "ready-rule",
                (sx.Symbol("ready"),),
                (),
            ),
        ),
        source=Path("<compiled-plan-wire-canary>"),
    )
    expected = bytes(
        [
            67, 71, 80, 49,
            1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0,
            5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 5, 0, 0, 0, 114, 101, 97, 100, 121,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            10, 0, 0, 0, 114, 101, 97, 100, 121, 45, 114, 117, 108, 101,
        ]
    )
    actual = language_generator.compile_plan((presentation,))
    if actual != expected:
        raise GateFailure("compiled-plan CGP1 wire canary changed")
    if sha256(actual).hexdigest() != (
        "9016f66beb8220e8c985e2e36dd17cacfcf824a987bf2275f335897084a04572"
    ):
        raise GateFailure("compiled-plan CGP1 wire canary digest changed")

    binary_presentation = sx.Presentation(
        name="CompiledPlanBinaryCanaryV1",
        operators=(sx.OperatorDecl("pair", 2),),
        rules=(
            sx.RuleDecl(
                "pair",
                (sx.Symbol("pair"), sx.Variable("x"), sx.Variable("y")),
                (),
            ),
        ),
        source=Path("<compiled-plan-binary-canary>"),
    )
    binary_expected = bytes(
        [
            67, 71, 80, 49,
            3, 0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0,
            2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0,
            2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 1, 0, 0, 0, 0, 0, 0, 0,
            5, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 4, 0, 0, 0, 112, 97, 105, 114,
            0, 0, 0, 0, 1, 0, 0, 0,
            2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0,
            4, 0, 0, 0, 112, 97, 105, 114,
        ]
    )
    binary_actual = language_generator.compile_plan((binary_presentation,))
    if binary_actual != binary_expected:
        raise GateFailure("compiled-plan binary CGP1 wire canary changed")
    if sha256(binary_actual).hexdigest() != (
        "ca23b221d503e538a0d668dc448284014c2d5db30258c3fc5399e6e6fb7cea79"
    ):
        raise GateFailure("compiled-plan binary CGP1 digest changed")


def generate(
    generator: Path,
    manifest: Path,
    header: Path,
    source: Path,
    symbol: str,
    header_include: str,
    source_root: Path | None = None,
    profile: str | None = None,
) -> None:
    command = [
        "python3",
        str(generator),
        "--manifest",
        str(manifest),
        "--header",
        str(header),
        "--source",
        str(source),
        "--symbol",
        symbol,
        "--header-include",
        header_include,
    ]
    if source_root:
        command.extend(("--source-root", str(source_root)))
    if profile:
        command.extend(("--profile", profile))
    completed = subprocess.run(
        command,
        text=True,
        capture_output=True,
        check=False,
    )
    if completed.returncode != 0:
        raise GateFailure(
            "language generation failed:\n"
            f"{completed.stdout}{completed.stderr}"
        )


def same_bytes(left: Path, right: Path, label: str) -> None:
    if left.read_bytes() != right.read_bytes():
        raise GateFailure(label)


def compiled_plan_digest(source: Path) -> str:
    match = re.search(
        r"\.compiled_plan\s*=\s*\{.*?\.sha256\s*=\s*"
        r'"([0-9a-f]{64})"',
        source.read_text(encoding="utf-8"),
        flags=re.DOTALL,
    )
    if not match:
        raise GateFailure("generated source omits the compiled-plan digest")
    return match.group(1)


def compiler_digest(source: Path) -> str:
    match = re.search(
        r'\.compiler_sha256\s*=\s*"([0-9a-f]{64})"',
        source.read_text(encoding="utf-8"),
    )
    if not match:
        raise GateFailure("generated source omits the compiler digest")
    return match.group(1)


def copy_language_inputs(
    manifest: Path,
    source_root: Path,
    destination: Path,
    profile: str | None,
) -> Path:
    """Copy only the declared language inputs, preserving source-root paths."""
    definition = language_generator.parse_manifest(manifest, profile)
    inputs = [manifest]
    inputs.extend(
        (manifest.parent / relative).resolve()
        for relative in definition.semantic_sources
    )
    for source in inputs:
        try:
            relative = source.relative_to(source_root)
        except ValueError as error:
            raise GateFailure(
                f"declared language input escapes source root: {source}"
            ) from error
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
    return destination / manifest.relative_to(source_root)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--generator", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--source-root", type=Path)
    parser.add_argument("--profile")
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--symbol", required=True)
    parser.add_argument("--header-include", required=True)
    arguments = parser.parse_args()

    check_compiled_plan_wire_canary()

    generator = arguments.generator.resolve()
    manifest = arguments.manifest.resolve()
    source_root = (
        arguments.source_root.resolve()
        if arguments.source_root
        else manifest.parent
    )
    checked_header = arguments.header.resolve()
    checked_source = arguments.source.resolve()
    with tempfile.TemporaryDirectory(prefix="gslt-language-generation-") as raw:
        temporary = Path(raw)
        first_header = temporary / "first.generated.h"
        first_source = temporary / "first.generated.c"
        second_header = temporary / "second.generated.h"
        second_source = temporary / "second.generated.c"
        generate(
            generator,
            manifest,
            first_header,
            first_source,
            arguments.symbol,
            arguments.header_include,
            source_root,
            arguments.profile,
        )
        generate(
            generator,
            manifest,
            second_header,
            second_source,
            arguments.symbol,
            arguments.header_include,
            source_root,
            arguments.profile,
        )
        same_bytes(first_header, second_header, "header generation is unstable")
        same_bytes(first_source, second_source, "source generation is unstable")
        same_bytes(
            first_header, checked_header,
            "checked-in language header is stale",
        )
        same_bytes(
            first_source, checked_source,
            "checked-in language source is stale",
        )

        copied_language = temporary / "language"
        copied_manifest = copy_language_inputs(
            manifest,
            source_root,
            copied_language,
            arguments.profile,
        )
        copied_definition = language_generator.parse_manifest(
            copied_manifest, arguments.profile
        )
        if not copied_definition.semantic_sources:
            raise GateFailure("language manifest has no semantic source")
        semantic_source = (
            copied_manifest.parent / copied_definition.semantic_sources[0]
        ).resolve()
        semantic_source.write_text(
            semantic_source.read_text(encoding="utf-8")
            + "\n; digest-closure mutation\n",
            encoding="utf-8",
        )
        mutated_header = temporary / "mutated.generated.h"
        mutated_source = temporary / "mutated.generated.c"
        generate(
            generator,
            copied_manifest,
            mutated_header,
            mutated_source,
            arguments.symbol,
            arguments.header_include,
            copied_language,
            arguments.profile,
        )
        if mutated_source.read_bytes() == first_source.read_bytes():
            raise GateFailure("semantic-source mutation did not change the pack")

        presentation = sx.parse_presentation(semantic_source)
        rule_index = next(
            (
                index
                for index, rule in enumerate(presentation.rules)
                if rule.body
            ),
            None,
        )
        if rule_index is None:
            raise GateFailure(
                "cannot locate a nonempty semantic rule for plan mutation"
            )
        mutated_rules = list(presentation.rules)
        selected_rule = mutated_rules[rule_index]
        mutated_rules[rule_index] = replace(
            selected_rule,
            body=selected_rule.body + (selected_rule.body[0],),
        )
        semantic_source.write_text(
            sx.canonical_text(replace(presentation, rules=tuple(mutated_rules))),
            encoding="utf-8",
        )
        plan_mutated_header = temporary / "plan-mutated.generated.h"
        plan_mutated_source = temporary / "plan-mutated.generated.c"
        generate(
            generator,
            copied_manifest,
            plan_mutated_header,
            plan_mutated_source,
            arguments.symbol,
            arguments.header_include,
            copied_language,
            arguments.profile,
        )
        if compiled_plan_digest(plan_mutated_source) == compiled_plan_digest(
            first_source
        ):
            raise GateFailure(
                "semantic-rule mutation did not change the compiled plan"
            )

        copied_compiler = temporary / "compiler"
        copied_compiler.mkdir()
        copied_generator = copied_compiler / generator.name
        copied_schema = copied_compiler / "gslt2parse_schema_v1.py"
        shutil.copy2(generator, copied_generator)
        shutil.copy2(generator.parent / copied_schema.name, copied_schema)
        copied_schema.write_text(
            copied_schema.read_text(encoding="utf-8")
            + "\n# compiler-closure mutation\n",
            encoding="utf-8",
        )
        compiler_mutated_header = temporary / "compiler-mutated.generated.h"
        compiler_mutated_source = temporary / "compiler-mutated.generated.c"
        generate(
            copied_generator,
            manifest,
            compiler_mutated_header,
            compiler_mutated_source,
            arguments.symbol,
            arguments.header_include,
            source_root,
            arguments.profile,
        )
        if compiler_digest(compiler_mutated_source) == compiler_digest(
            first_source
        ):
            raise GateFailure(
                "schema-compiler mutation did not change compiler authority"
            )

    print(
        "(GsltLanguageGenerationV1Summary "
        "deterministic=1 checked-in=1 semantic-digest-closure=1 "
        "compiled-plan-closure=1 compiler-closure=1 wire-canary=1)"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (GateFailure, OSError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
