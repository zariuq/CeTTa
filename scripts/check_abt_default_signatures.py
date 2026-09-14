#!/usr/bin/env python3
"""Check that native ABT defaults are compiled from the importable MeTTa atom."""

from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "lib" / "abt_default_signatures.metta"
SUMMARY = "(ABTDefaultSignatureSourceSummary 2 2 0)"
PROBES = f"""
!(assertEqual
   (__cetta_abt_default_signatures AbtDefaultsV1)
   (abt-default-signatures))
!(assertEqual
   (__cetta_abt_signature_admitted (AbtSignatures))
   True)
!(println! {SUMMARY})
"""
MUTATION_MARKER = """(AbtSignature Pi
       (Fields domain codomain)
       (BindPi domain codomain))"""
MUTATION_REPLACEMENT = """(AbtSignature Pi
       (Fields domain codomain)
       Bind0)"""


def run_source(binary: Path, source: str, language_args: list[str]) -> str:
    runtime = ROOT / "runtime"
    runtime.mkdir(exist_ok=True)
    generated: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            suffix=".metta",
            prefix="abt-default-signatures-",
            dir=runtime,
            delete=False,
        ) as handle:
            handle.write(source)
            generated = Path(handle.name)
        completed = subprocess.run(
            [str(binary), *language_args, str(generated)],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=30,
            check=False,
        )
    finally:
        if generated is not None:
            generated.unlink(missing_ok=True)
    if completed.returncode != 0:
        raise RuntimeError(
            f"CeTTa exited {completed.returncode}:\n"
            f"{completed.stderr}{completed.stdout}"
        )
    return "\n".join(
        line.strip()
        for line in completed.stdout.splitlines()
        if line.strip() and not line.startswith("Failed to create stream fd:")
    )


def require_exact_positive(output: str, lane: str) -> None:
    lines = output.splitlines()
    units = sum(line == "[()]" for line in lines)
    summaries = sum(line == SUMMARY for line in lines)
    unexpected = [line for line in lines if line not in {"[()]", SUMMARY}]
    if units != 3 or summaries != 1 or unexpected:
        raise RuntimeError(
            f"{lane} source/native identity produced {units}/3 units, "
            f"{summaries}/1 summaries, unexpected={unexpected[:4]}"
        )


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {Path(sys.argv[0]).name} CETTA", file=sys.stderr)
        return 2
    binary = Path(sys.argv[1]).resolve()
    if not binary.is_file():
        print(f"missing CeTTa binary: {binary}", file=sys.stderr)
        return 2

    source = SOURCE.read_text(encoding="utf-8")
    for lane, args in (
        ("Prime", ["--lang", "prime"]),
        ("HE-extended", ["--lang", "he", "--profile", "extended"]),
    ):
        require_exact_positive(run_source(binary, source + PROBES, args), lane)

    if source.count(MUTATION_MARKER) != 1:
        raise RuntimeError("default-signature mutation marker drifted")
    mutant = source.replace(MUTATION_MARKER, MUTATION_REPLACEMENT)
    mutant_output = run_source(
        binary, mutant + PROBES, ["--lang", "prime"]
    )
    if mutant_output.splitlines().count("[()]") == 3 or \
            "Error" not in mutant_output:
        raise RuntimeError(
            "binding-declaration source mutation did not break native identity"
        )

    print("PASS: ABT default declarations have one MeTTa source; drift mutant killed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
