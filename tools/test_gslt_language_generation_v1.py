#!/usr/bin/env python3
"""Check deterministic, digest-closed GSLT language-pack generation."""

from __future__ import annotations

import argparse
from dataclasses import replace
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

import generate_gslt_language_v1 as language_generator
import gslt2parse_schema_v1 as sx


class GateFailure(RuntimeError):
    pass


def generate(
    generator: Path,
    manifest: Path,
    header: Path,
    source: Path,
    symbol: str,
    header_include: str,
    source_root: Path | None = None,
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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--generator", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--source-root", type=Path)
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--symbol", required=True)
    parser.add_argument("--header-include", required=True)
    arguments = parser.parse_args()

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
        )
        generate(
            generator,
            manifest,
            second_header,
            second_source,
            arguments.symbol,
            arguments.header_include,
            source_root,
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
        shutil.copytree(source_root, copied_language)
        copied_manifest = copied_language / manifest.relative_to(source_root)
        copied_definition = language_generator.parse_manifest(copied_manifest)
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
        "compiled-plan-closure=1 compiler-closure=1)"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (GateFailure, OSError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
