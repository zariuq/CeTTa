#!/usr/bin/env python3
"""Run the C ABT core against the authoritative import-free MeTTa waist."""

from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parents[1]
FIXTURE = ROOT / "tests" / "abt" / "differential.metta"
WAIST_ORACLE = ROOT / "tests" / "abt" / "waist_oracle.metta"
WAIST_RELATIVE = Path("MettaKernel/kernel/kernel_binding_waist_v1.metta")
PETTA_WAIST = "kernel_binding_waist_v1_petta.metta"
PETTA_REPLAY = "kernel_binding_abtg_replay_v1_petta.metta"
PETTA_DEPENDENCIES = (
    "kernel_binding_waist_v1_petta.metta",
    "kernel_binding_decl_v1_petta.metta",
    PETTA_REPLAY,
)

# Prime's canonical `(App f x)` is executable syntax.  The ABT differential
# compares transformations of syntax data, so its Prime lane uses an
# isomorphic, explicitly admitted constructor rather than accidentally asking
# the evaluator to run the transformed tree.  HE keeps the canonical corpus;
# only the Prime test presentation is renamed.
PRIME_DATA_APP = "ABTApp"
PRIME_DATA_APP_SIGNATURE = (
    "(AbtSignatures "
    "(AbtSignature ABTApp (Fields fn arg) "
    "(BindFields (BCons (BField fn 0) "
    "(BCons (BField arg 0) BNil)))))"
)

CANONICAL_ORACLE_PRELUDE = r"""
(= (oracle-shift $d $c $t) (bind-shift $d $c $t))
(= (oracle-shift-args $d $c $t) (bind-shift-args $d $c $t))
(= (oracle-shift-cases $d $c $t) (bind-shift-cases $d $c $t))
(= (oracle-subst $j $s $t) (bind-subst $j $s $t))
(= (oracle-depth-shift $t) (bind-shift 1 3 $t))
(= (oracle-depth-subst $s $t)
   (bind-subst 3 (bind-shift 3 0 $s) $t))
"""

NATIVE_ORACLE_PRELUDE = r"""
(= (oracle-depth-signature)
   (AbtSignatures
     (AbtSignature Scoped (Fields body)
       (BindFields (BCons (BField body 3) BNil)))))
(= (oracle-shift $d $c $t)
   (__cetta_abt_shift (AbtSignatures) $d $c $t))
(= (oracle-shift-args $d $c $t)
   (__cetta_abt_shift (AbtSignatures) $d $c $t))
(= (oracle-shift-cases $d $c $t)
   (__cetta_abt_shift (AbtSignatures) $d $c $t))
(= (oracle-subst $j $s $t)
   (__cetta_abt_subst (AbtSignatures) $j $s $t))
(= (oracle-depth-shift $t)
   (let $result
     (__cetta_abt_shift (oracle-depth-signature) 1 0 (Scoped $t))
     (case $result
       (((Scoped $shifted) $shifted)
        ($_ (Bad native-depth-shift))))))
(= (oracle-depth-subst $s $t)
   (let $result
     (__cetta_abt_subst (oracle-depth-signature) 0 $s (Scoped $t))
     (case $result
       (((Scoped $substituted) $substituted)
        ($_ (Bad native-depth-subst))))))
"""

PETTA_ORACLE_PRELUDE = r"""
(= (oracle_shift $d $c $t) (bind_shift $d $c $t))
(= (oracle_shift_args $d $c $t) (bind_shift_args $d $c $t))
(= (oracle_shift_cases $d $c $t) (bind_shift_cases $d $c $t))
(= (oracle_subst $j $s $t) (bind_subst $j $s $t))
(= (oracle_depth_shift $t) (bind_shift 1 3 $t))
(= (oracle_depth_subst $s $t)
   (bind_subst 3 (bind_shift 3 0 $s) $t))
"""


def find_waist() -> Path | None:
    candidates: list[Path] = []
    override = os.environ.get("METTAPEDIA_ROOT")
    if override:
        base = Path(override).expanduser().resolve()
        candidates.extend([base / WAIST_RELATIVE, base / "kernel_binding_waist_v1.metta"])
    candidates.append(WORKSPACE / "Mettapedia" / WAIST_RELATIVE)
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return None


def prime_syntax_data_source(source: str) -> str:
    """Present canonical ABT application as inert syntax in Prime.

    This is a constructor renaming plus a matching binding declaration, not a
    second shift/substitution oracle.  Every equation and expected term comes
    from the same source used by the HE lane.
    """
    transformed, app_count = re.subn(r"\(App(?=\s)", "(ABTApp", source)
    signature_count = transformed.count("(AbtSignatures)")
    if app_count == 0 or signature_count == 0:
        raise RuntimeError(
            "ABT Prime data presentation lost its App/signature anchors"
        )
    return transformed.replace("(AbtSignatures)", PRIME_DATA_APP_SIGNATURE)


def run_lane(
    binary: Path,
    program: Path,
    args: list[str],
    expected_pass_markers: int,
    expected_summary: str,
) -> None:
    completed = subprocess.run(
        [str(binary), *args, str(program)],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=60,
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"ABT differential lane {' '.join(args)} exited "
            f"{completed.returncode}:\n{completed.stderr}{completed.stdout}"
        )
    lines = [line.strip() for line in completed.stdout.splitlines() if line.strip()]
    assertion_count = sum(line == "[()]" for line in lines)
    summary_count = sum(line == expected_summary for line in lines)
    bad = [
        line for line in lines
        if line != "[()]" and line != expected_summary
    ]
    if (
        assertion_count != expected_pass_markers
        or summary_count != 1
        or len(lines) != expected_pass_markers + 1
        or bad
    ):
        detail = "\n".join(lines[-20:])
        raise RuntimeError(
            f"ABT differential lane {' '.join(args)} produced "
            f"{assertion_count}/{expected_pass_markers} passing evaluations and "
            f"{summary_count}/1 exact summaries; unexpected lines: "
            f"{bad[:5]}\n{detail}\n{completed.stderr}"
        )


def find_petta() -> Path | None:
    override = os.environ.get("PETTA_ROOT")
    candidates = []
    if override:
        base = Path(override).expanduser().resolve()
        candidates.extend([base / "run.sh", base])
    candidates.append(WORKSPACE / "hyperon" / "PeTTa" / "run.sh")
    return next((candidate for candidate in candidates if candidate.is_file()), None)


def run_source(command: list[str], source: str, timeout: int) -> subprocess.CompletedProcess[str]:
    runtime = ROOT / "runtime"
    runtime.mkdir(exist_ok=True)
    generated: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            suffix=".metta",
            prefix="abt-oracle-",
            dir=runtime,
            delete=False,
        ) as handle:
            handle.write(source)
            generated = Path(handle.name)
        return subprocess.run(
            [*command, str(generated)],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
            check=False,
        )
    finally:
        if generated is not None:
            generated.unlink(missing_ok=True)


def oracle_lines(output: str) -> list[str]:
    ansi = re.compile(r"\x1b\[[0-9;]*m")
    lines = []
    for raw_line in output.splitlines():
        line = ansi.sub("", raw_line).strip()
        start = line.find("(ABTWaistOracle")
        if start >= 0:
            candidate = line[start:]
            if candidate.count("(") == candidate.count(")"):
                lines.append(candidate)
    return lines


def prime_data_oracle_lines(output: str) -> list[str]:
    """Map the inert Prime test presentation back to canonical ABT spelling."""
    return [
        re.sub(r"\(ABTApp(?=\s)", "(App", line)
        for line in oracle_lines(output)
    ]


def run_three_way_oracle(binary: Path, waist: Path) -> bool:
    corpus = WAIST_ORACLE.read_text(encoding="utf-8")
    case_count = len(re.findall(r"ABTWaistOracle\s+s[0-9]+", corpus))
    expected_summary = f"(ABTWaistOracleSummary {case_count} {case_count} 0)"
    if case_count != 13 or corpus.count(expected_summary) != 1:
        raise RuntimeError("ABT waist oracle corpus count or summary drifted")

    native = run_source(
        [str(binary), "--lang", "prime"],
        prime_syntax_data_source(NATIVE_ORACLE_PRELUDE + "\n" + corpus),
        60,
    )
    canonical = run_source(
        [str(binary), "--profile", "he-extended", "--lang", "he"],
        waist.read_text(encoding="utf-8")
        + "\n"
        + CANONICAL_ORACLE_PRELUDE
        + "\n"
        + corpus,
        60,
    )
    for lane, completed in (("native C", native), ("CeTTa waist", canonical)):
        if completed.returncode != 0:
            raise RuntimeError(
                f"{lane} ABT waist oracle exited {completed.returncode}:\n"
                f"{completed.stderr}{completed.stdout}"
            )
    native_lines = prime_data_oracle_lines(native.stdout)
    canonical_lines = oracle_lines(canonical.stdout)
    if (
        len(native_lines) != case_count + 1
        or native_lines[-1:] != [expected_summary]
        or native_lines != canonical_lines
    ):
        raise RuntimeError(
            "native C and CeTTa waist oracle outputs differ:\n"
            f"native={native_lines}\ncanonical={canonical_lines}"
        )

    launcher = find_petta()
    petta_waist = waist.parent / PETTA_WAIST
    if launcher is None or not petta_waist.is_file():
        print("SKIP: PeTTa shared ABT corpus (set PETTA_ROOT to a PeTTa checkout)")
        print(
            f"PASS: native C/CeTTa waist shared corpus "
            f"({case_count}/{case_count} cases)"
        )
        return False

    petta_corpus = re.sub(
        r"\boracle-[a-z-]+",
        lambda match: match.group(0).replace("-", "_"),
        corpus,
    )
    petta = run_source(
        ["bash", str(launcher)],
        petta_waist.read_text(encoding="utf-8")
        + "\n"
        + PETTA_ORACLE_PRELUDE
        + "\n"
        + petta_corpus,
        180,
    )
    petta_lines = oracle_lines(petta.stdout)
    if petta.returncode != 0 or petta_lines != native_lines:
        raise RuntimeError(
            f"PeTTa shared ABT corpus differs or exited {petta.returncode}:\n"
            f"native={native_lines}\npetta={petta_lines}\n"
            f"{petta.stderr}{petta.stdout[-4000:]}"
        )
    print(
        f"PASS: native C/CeTTa waist/PeTTa shared corpus "
        f"({case_count}/{case_count} cases)"
    )
    return True


def run_petta_replay(waist: Path) -> bool:
    launcher = find_petta()
    kernel = waist.parent
    replay = kernel / PETTA_REPLAY
    dependencies = [kernel / name for name in PETTA_DEPENDENCIES]
    if launcher is None or not replay.is_file() or not all(
        dependency.is_file() for dependency in dependencies
    ):
        print("SKIP: PeTTa ABT replay (set PETTA_ROOT to a PeTTa checkout)")
        return False

    expected = sum(
        len(re.findall(r"!\s*\(test\b", path.read_text(encoding="utf-8")))
        for path in dependencies
    )
    completed = subprocess.run(
        ["bash", str(launcher), str(replay)],
        cwd=kernel,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=180,
        check=False,
    )
    checkmarks = completed.stdout.count("✅")
    failed_checks = [
        line for line in completed.stdout.splitlines() if "❌" in line
    ]
    if completed.returncode != 0 or checkmarks != expected or failed_checks:
        tail = "\n".join(completed.stdout.splitlines()[-30:])
        raise RuntimeError(
            "PeTTa ABT replay produced "
            f"{checkmarks}/{expected} successful checks and exited "
            f"{completed.returncode}; failed checks: {failed_checks[:5]}\n"
            f"{tail}\n{completed.stderr}"
        )
    print(f"PASS: PeTTa ABT replay oracle ({expected}/{expected} checks)")
    return True


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {Path(sys.argv[0]).name} CETTA", file=sys.stderr)
        return 2
    binary = Path(sys.argv[1]).resolve()
    if not binary.is_file():
        print(f"missing CeTTa binary: {binary}", file=sys.stderr)
        return 2
    waist = find_waist()
    if waist is None:
        print("SKIP: ABT differential (set METTAPEDIA_ROOT to a Mettapedia checkout)")
        return 0

    source = waist.read_text(encoding="utf-8") + "\n" + FIXTURE.read_text(encoding="utf-8")
    fixture_assertions = len(
        re.findall(
            r"!\s*\(assertEqual(?:ToResult)?\b",
            FIXTURE.read_text(encoding="utf-8"),
        )
    )
    expected = len(re.findall(r"!\s*\(assertEqual(?:ToResult)?\b", source))
    summary = (
        f"(ABTDifferentialSummary {fixture_assertions} "
        f"{fixture_assertions} 0)"
    )
    runtime = ROOT / "runtime"
    runtime.mkdir(exist_ok=True)
    generated: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", suffix=".metta",
            prefix="abt-differential-", dir=runtime, delete=False,
        ) as handle:
            handle.write(source)
            generated = Path(handle.name)
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", suffix=".metta",
            prefix="abt-differential-prime-data-", dir=runtime, delete=False,
        ) as handle:
            handle.write(prime_syntax_data_source(source))
            generated_prime = Path(handle.name)
        run_lane(
            binary,
            generated,
            ["--profile", "he-extended", "--lang", "he"],
            expected + 1,
            summary,
        )
        run_lane(
            binary, generated_prime, ["--lang", "prime"], expected + 1,
            summary,
        )
    finally:
        if generated is not None:
            generated.unlink(missing_ok=True)
        if generated_prime is not None:
            generated_prime.unlink(missing_ok=True)

    shared_petta_ran = run_three_way_oracle(binary, waist)
    petta_ran = run_petta_replay(waist)
    if shared_petta_ran != petta_ran:
        raise RuntimeError("PeTTa availability changed during the ABT differential")
    suffix = " + PeTTa replay" if petta_ran else ""
    print(
        f"PASS: C/MeTTa ABT differential "
        f"({expected} assertions x 2 CeTTa lanes{suffix})"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
