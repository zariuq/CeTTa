#!/usr/bin/env python3
"""Gate deterministic and digest-closed support-transform profile generation."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
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
    expect_success: bool = True,
) -> subprocess.CompletedProcess[str]:
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
    if expect_success and completed.returncode != 0:
        raise GateFailure(
            "support-transform generation failed:\n"
            f"{completed.stdout}{completed.stderr}"
        )
    if not expect_success and completed.returncode == 0:
        raise GateFailure("invalid support-transform profile was accepted")
    return completed


def digest(source: Path, field: str) -> str:
    match = re.search(
        rf'\.{re.escape(field)}\s*=\s*"([0-9a-f]{{64}})"',
        source.read_text(encoding="utf-8"),
    )
    if not match:
        raise GateFailure(f"generated source omits {field}")
    return match.group(1)


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
    with tempfile.TemporaryDirectory(
        prefix="gslt-support-transform-generation-"
    ) as raw:
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
        if first_header.read_bytes() != second_header.read_bytes() or (
            first_source.read_bytes() != second_source.read_bytes()
        ):
            raise GateFailure("support-transform generation is unstable")
        if first_header.read_bytes() != checked_header.read_bytes() or (
            first_source.read_bytes() != checked_source.read_bytes()
        ):
            raise GateFailure("checked-in support-transform descriptor is stale")

        mutated_manifest = temporary / "mutated.metta"
        original_text = manifest.read_text(encoding="utf-8")
        mutated_manifest.write_text(
            original_text.replace("(work-shell exec ", "(work-shell work ", 1),
            encoding="utf-8",
        )
        mutated_header = temporary / "mutated.generated.h"
        mutated_source = temporary / "mutated.generated.c"
        generate(
            generator,
            mutated_manifest,
            mutated_header,
            mutated_source,
            arguments.symbol,
            arguments.header_include,
        )
        if digest(first_source, "manifest_sha256") == digest(
            mutated_source, "manifest_sha256"
        ):
            raise GateFailure("manifest mutation did not change its identity")
        if '.work_symbol = "work"' not in mutated_source.read_text(
            encoding="utf-8"
        ):
            raise GateFailure("manifest vocabulary mutation did not reach the pack")

        invalid_manifest = temporary / "invalid.metta"
        invalid_manifest.write_text(
            original_text.replace("(unsupported leave-inert)",
                                  "(unsupported consume)", 1),
            encoding="utf-8",
        )
        generate(
            generator,
            invalid_manifest,
            temporary / "invalid.generated.h",
            temporary / "invalid.generated.c",
            arguments.symbol,
            arguments.header_include,
            expect_success=False,
        )

        compiler_dir = temporary / "compiler"
        compiler_dir.mkdir()
        copied_generator = compiler_dir / generator.name
        copied_schema = compiler_dir / "gslt2parse_schema_v1.py"
        shutil.copy2(generator, copied_generator)
        shutil.copy2(generator.parent / copied_schema.name, copied_schema)
        copied_schema.write_text(
            copied_schema.read_text(encoding="utf-8")
            + "\n# compiler identity mutation\n",
            encoding="utf-8",
        )
        compiler_header = temporary / "compiler.generated.h"
        compiler_source = temporary / "compiler.generated.c"
        generate(
            copied_generator,
            manifest,
            compiler_header,
            compiler_source,
            arguments.symbol,
            arguments.header_include,
        )
        if digest(first_source, "compiler_sha256") == digest(
            compiler_source, "compiler_sha256"
        ):
            raise GateFailure("compiler mutation did not change its identity")

    print(
        "(GsltSupportTransformGenerationV1Summary "
        "deterministic=1 checked-in=1 manifest-closure=1 "
        "compiler-closure=1 invalid-policy-rejected=1)"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (GateFailure, OSError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
