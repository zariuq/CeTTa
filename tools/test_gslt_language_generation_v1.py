#!/usr/bin/env python3
"""Check deterministic, digest-closed GSLT language-pack generation."""

from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


class GateFailure(RuntimeError):
    pass


def generate(
    generator: Path,
    manifest: Path,
    header: Path,
    source: Path,
    symbol: str,
    header_include: str,
) -> None:
    completed = subprocess.run(
        [
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
        ],
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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--generator", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--symbol", required=True)
    parser.add_argument("--header-include", required=True)
    arguments = parser.parse_args()

    generator = arguments.generator.resolve()
    manifest = arguments.manifest.resolve()
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
        )
        generate(
            generator,
            manifest,
            second_header,
            second_source,
            arguments.symbol,
            arguments.header_include,
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
        shutil.copytree(manifest.parent, copied_language)
        copied_manifest = copied_language / manifest.name
        semantic_source = copied_language / "semantics" / (
            "free_bag_rewrite_core_v1.metta"
        )
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
        )
        if mutated_source.read_bytes() == first_source.read_bytes():
            raise GateFailure("semantic-source mutation did not change the pack")

    print(
        "(GsltLanguageGenerationV1Summary "
        "deterministic=1 checked-in=1 semantic-digest-closure=1)"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (GateFailure, OSError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
