#!/usr/bin/env python3
"""Check deterministic, digest-closed GSLT provider-catalog generation."""

from __future__ import annotations

import argparse
from hashlib import sha256
from pathlib import Path
import subprocess
import tempfile


class GateFailure(RuntimeError):
    pass


def generate(
    generator: Path,
    catalog: Path,
    language_manifest: Path,
    source_root: Path | None,
    header: Path,
    source: Path,
    symbol: str,
    header_include: str,
) -> None:
    command = [
        "python3",
        str(generator),
        "--catalog",
        str(catalog),
        "--language-manifest",
        str(language_manifest),
        "--header",
        str(header),
        "--source",
        str(source),
        "--symbol",
        symbol,
        "--header-include",
        header_include,
    ]
    if source_root is not None:
        command.extend(("--source-root", str(source_root)))
    completed = subprocess.run(
        command,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if completed.returncode != 0:
        raise GateFailure(completed.stdout.strip() or "catalog generation failed")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--generator", type=Path, required=True)
    parser.add_argument("--catalog", type=Path, required=True)
    parser.add_argument("--language-manifest", type=Path, required=True)
    parser.add_argument("--source-root", type=Path)
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--symbol", required=True)
    parser.add_argument("--header-include", required=True)
    arguments = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="cetta-provider-catalog-") as raw:
        directory = Path(raw)
        first_h = directory / "first.h"
        first_c = directory / "first.c"
        second_h = directory / "second.h"
        second_c = directory / "second.c"
        generate(
            arguments.generator,
            arguments.catalog,
            arguments.language_manifest,
            arguments.source_root,
            first_h,
            first_c,
            arguments.symbol,
            arguments.header_include,
        )
        generate(
            arguments.generator,
            arguments.catalog,
            arguments.language_manifest,
            arguments.source_root,
            second_h,
            second_c,
            arguments.symbol,
            arguments.header_include,
        )
        if first_h.read_bytes() != second_h.read_bytes() or first_c.read_bytes() != second_c.read_bytes():
            raise GateFailure("provider-catalog generation is nondeterministic")
        if first_h.read_bytes() != arguments.header.read_bytes():
            raise GateFailure("checked-in provider-catalog header is stale")
        if first_c.read_bytes() != arguments.source.read_bytes():
            raise GateFailure("checked-in provider-catalog source is stale")

        generated = first_c.read_text(encoding="utf-8")
        catalog_digest = sha256(arguments.catalog.read_bytes()).hexdigest()
        manifest_digest = sha256(arguments.language_manifest.read_bytes()).hexdigest()
        if catalog_digest not in generated:
            raise GateFailure("provider catalog is not digest-closed")
        if manifest_digest not in generated:
            raise GateFailure("target language manifest is not digest-closed")

    print(
        "(GsltProviderCatalogGenerationV1Summary "
        "deterministic=1 checked-in=1 catalog-digest-closure=1 "
        "language-manifest-closure=1)"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (GateFailure, OSError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
