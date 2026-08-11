#!/usr/bin/env python3
"""Exercise deterministic GSLT compilation certificates and fail-closed checks."""

from __future__ import annotations

import argparse
from hashlib import sha256
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

import generate_gslt_language_v1 as language_compiler


class GateFailure(RuntimeError):
    pass


def run(command: list[str], *, expect_success: bool) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(
        command, text=True, capture_output=True, check=False
    )
    succeeded = completed.returncode == 0
    if succeeded != expect_success:
        expectation = "accept" if expect_success else "reject"
        raise GateFailure(
            f"expected certificate checker to {expectation}:\n"
            f"{completed.stdout}{completed.stderr}"
        )
    return completed


def producer_command(arguments: argparse.Namespace, output: Path) -> list[str]:
    command = [
        "python3",
        str(arguments.producer),
        "--manifest",
        str(arguments.manifest),
        "--source-root",
        str(arguments.source_root),
        "--header",
        str(arguments.header),
        "--source",
        str(arguments.source),
        "--symbol",
        arguments.symbol,
        "--generator",
        str(arguments.generator),
        "--schema",
        str(arguments.schema),
        "--certificate",
        str(output),
    ]
    if arguments.profile != "base":
        command.extend(("--profile", arguments.profile))
    return command


def checker_command(
    arguments: argparse.Namespace,
    certificate: Path,
    *,
    source_root: Path | None = None,
    source: Path | None = None,
    schema: Path | None = None,
) -> list[str]:
    return [
        str(arguments.checker),
        arguments.profile,
        str(certificate),
        str(source_root or arguments.source_root),
        str(arguments.header),
        str(source or arguments.source),
        str(arguments.generator),
        str(schema or arguments.schema),
    ]


def file_sha(path: Path) -> str:
    return sha256(path.read_bytes()).hexdigest()


def mutate_plan_digest(source: str) -> str:
    pattern = re.compile(
        r'(\(compiled-plan "CGP1" [0-9]+ ")([0-9a-f])([0-9a-f]{63}"\))'
    )
    match = pattern.search(source)
    if not match:
        raise GateFailure("cannot locate the compiled-plan identity")
    replacement = "0" if match.group(2) != "0" else "1"
    return source[: match.start()] + (
        match.group(1) + replacement + match.group(3)
    ) + source[match.end() :]


def mutate_observation(source: str) -> str:
    pattern = re.compile(r'(\(observation ")([^"]+)("\))')
    match = pattern.search(source)
    if not match:
        raise GateFailure("cannot locate the observation contract")
    replacement = "support" if match.group(2) != "support" else "bag"
    return source[: match.start()] + (
        match.group(1) + replacement + match.group(3)
    ) + source[match.end() :]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--checker", type=Path, required=True)
    parser.add_argument("--producer", type=Path, required=True)
    parser.add_argument("--generator", type=Path, required=True)
    parser.add_argument("--schema", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument(
        "--profile", choices=("base", "exp", "emit", "interact"), required=True
    )
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--symbol", required=True)
    parser.add_argument("--certificate", type=Path, required=True)
    arguments = parser.parse_args()

    for field in (
        "checker",
        "producer",
        "generator",
        "schema",
        "manifest",
        "source_root",
        "header",
        "source",
        "certificate",
    ):
        setattr(arguments, field, getattr(arguments, field).resolve())

    artifact_before = (file_sha(arguments.header), file_sha(arguments.source))
    with tempfile.TemporaryDirectory(
        prefix=f"gslt-certificate-{arguments.profile}-"
    ) as raw:
        temporary = Path(raw)
        first = temporary / "first.metta"
        second = temporary / "second.metta"
        run(producer_command(arguments, first), expect_success=True)
        run(producer_command(arguments, second), expect_success=True)
        if first.read_bytes() != second.read_bytes():
            raise GateFailure("certificate production is nondeterministic")
        if first.read_bytes() != arguments.certificate.read_bytes():
            raise GateFailure("checked-in compilation certificate is stale")
        artifact_after = (file_sha(arguments.header), file_sha(arguments.source))
        if artifact_after != artifact_before:
            raise GateFailure("certificate production changed the runtime artifact")

        accepted = run(
            checker_command(arguments, first), expect_success=True
        )
        if "GsltCompilationCertificateV1Accepted" not in accepted.stdout:
            raise GateFailure("native checker emitted no acceptance receipt")

        corrupt_certificate = temporary / "corrupt-plan.metta"
        corrupt_certificate.write_text(
            mutate_plan_digest(first.read_text(encoding="utf-8")),
            encoding="utf-8",
        )
        run(
            checker_command(arguments, corrupt_certificate),
            expect_success=False,
        )

        corrupt_observation = temporary / "corrupt-observation.metta"
        corrupt_observation.write_text(
            mutate_observation(first.read_text(encoding="utf-8")),
            encoding="utf-8",
        )
        run(
            checker_command(arguments, corrupt_observation),
            expect_success=False,
        )

        copied_root = temporary / "source-root"
        shutil.copytree(arguments.source_root, copied_root)
        copied_manifest = copied_root / arguments.manifest.relative_to(
            arguments.source_root
        )
        selected_profile = None if arguments.profile == "base" else arguments.profile
        manifest = language_compiler.parse_manifest(
            copied_manifest, selected_profile
        )
        selected_source = (
            copied_manifest.parent / manifest.semantic_sources[0]
        ).resolve()
        selected_source.write_text(
            selected_source.read_text(encoding="utf-8")
            + "\n; certificate source-tamper canary\n",
            encoding="utf-8",
        )
        run(
            checker_command(arguments, first, source_root=copied_root),
            expect_success=False,
        )

        corrupt_artifact = temporary / "corrupt.generated.c"
        corrupt_artifact.write_bytes(
            arguments.source.read_bytes() + b"\n/* artifact tamper */\n"
        )
        run(
            checker_command(arguments, first, source=corrupt_artifact),
            expect_success=False,
        )

        corrupt_schema = temporary / "gslt2parse_schema_v1.py"
        corrupt_schema.write_bytes(
            arguments.schema.read_bytes() + b"\n# compiler tamper\n"
        )
        run(
            checker_command(arguments, first, schema=corrupt_schema),
            expect_success=False,
        )

    print(
        "(GsltCompilationCertificateV1Summary "
        f"profile={arguments.profile} deterministic=1 artifact-erasure=1 "
        "accepted=1 tamper-rejections=5)"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (GateFailure, OSError, ValueError) as error:
        print(f"FAIL: {error}")
        raise SystemExit(1)
