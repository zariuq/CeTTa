#!/usr/bin/env python3
"""Pin parse-only HE/PeTTa authority receipts and emit optional timing observations."""

from __future__ import annotations

import argparse
from hashlib import sha256
import json
from pathlib import Path
import statistics
import subprocess
import sys
from typing import Any

from run_he_parser_oracle_v1 import authority_digest as he_authority_digest
from run_he_parser_oracle_v1 import authority_revision as he_authority_revision
from test_he_parser_authority_v1 import EXPECTED_REVISION as EXPECTED_HE_REVISION
from test_he_parser_authority_v1 import EXPECTED_SOURCE_DIGEST as EXPECTED_HE_DIGEST
from test_petta_parser_authority_v1 import EXPECTED_PARSER_DIGEST
from test_petta_parser_authority_v1 import EXPECTED_READER_DIGEST
from test_petta_parser_authority_v1 import EXPECTED_REVISION as EXPECTED_PETTA_REVISION
from test_petta_parser_authority_v1 import file_digest
from test_petta_parser_authority_v1 import revision as petta_revision


ROOT = Path(__file__).resolve().parents[1]
HE_SOURCE = ROOT / "tools" / "he_parser_parse_only_v1.rs"
PETTA_SOURCE = ROOT / "tools" / "petta_parser_parse_only_v1.pl"
BUILD_ROOT = ROOT / "runtime" / "bootstrap" / "he_parser_parse_only_v1"
CORPUS_ROOT = ROOT / "runtime" / "bootstrap" / "stable_parser_parse_only_v1"
EXPECTED_MATRIX_DIGEST = "7aa72ac52e8cd2c245aff9b29f34b3a5be3a877cfb303d284c67eb946b319c2f"


class GateFailure(RuntimeError):
    pass


def run_checked(
    command: list[str],
    *,
    cwd: Path,
    timeout: int = 900,
) -> subprocess.CompletedProcess[bytes]:
    completed = subprocess.run(
        command,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        timeout=timeout,
    )
    if completed.returncode != 0:
        raise GateFailure(
            f"command failed ({completed.returncode}): {' '.join(command)}\n"
            + completed.stderr.decode("utf-8", errors="replace")
        )
    return completed


def manifest_text(he_root: Path) -> str:
    hyperon_path = json.dumps(str(he_root / "lib"))
    atom_path = json.dumps(str(he_root / "hyperon-atom"))
    return f"""[package]
name = "he-parser-parse-only-v1"
version = "0.0.0"
edition = "2021"

[dependencies]
hyperon = {{ path = {hyperon_path}, default-features = false, features = ["pkg_mgmt"] }}
hyperon-atom = {{ path = {atom_path} }}

[[bin]]
name = "he-parser-parse-only-v1"
path = "src/main.rs"
"""


def build_he_helper(he_root: Path) -> Path:
    source_dir = BUILD_ROOT / "src"
    source_dir.mkdir(parents=True, exist_ok=True)
    manifest = BUILD_ROOT / "Cargo.toml"
    generated_source = source_dir / "main.rs"
    expected_manifest = manifest_text(he_root)
    expected_source = HE_SOURCE.read_bytes()
    if not manifest.exists() or manifest.read_text(encoding="utf-8") != expected_manifest:
        manifest.write_text(expected_manifest, encoding="utf-8")
    if not generated_source.exists() or generated_source.read_bytes() != expected_source:
        generated_source.write_bytes(expected_source)
    run_checked(
        [
            "cargo",
            "build",
            "--offline",
            "--quiet",
            "--release",
            "--manifest-path",
            str(manifest),
        ],
        cwd=BUILD_ROOT,
    )
    binary = BUILD_ROOT / "target" / "release" / "he-parser-parse-only-v1"
    if not binary.is_file():
        raise GateFailure("Cargo succeeded without producing the HE parse-only helper")
    return binary


def write_generated_corpora() -> dict[str, Path]:
    CORPUS_ROOT.mkdir(parents=True, exist_ok=True)
    many_forms = ("(f $x $x \"a\\n\" λ)\n" * 4096).encode("utf-8")
    wide_form = ("(" + " ".join(["word"] * 8192) + ")\n").encode("utf-8")
    deep_form = (("(" * 256 + "z" + ")" * 256 + "\n") * 32).encode("utf-8")
    corpora = {
        "many-forms": many_forms,
        "wide-form": wide_form,
        "deep-forms": deep_form,
    }
    paths: dict[str, Path] = {}
    for label, content in corpora.items():
        path = CORPUS_ROOT / f"{label}.metta"
        if not path.exists() or path.read_bytes() != content:
            path.write_bytes(content)
        paths[label] = path
    return paths


def corpus_cases(
    he_root: Path,
    petta_root: Path,
) -> list[tuple[str, str, Path]]:
    generated = write_generated_corpora()
    return [
        (
            "he-stdlib",
            "he",
            he_root / "lib" / "src" / "metta" / "runner" / "stdlib" / "stdlib.metta",
        ),
        ("he-test-stdlib", "he", he_root / "lib" / "tests" / "test_stdlib.metta"),
        (
            "he-das",
            "he",
            he_root / "lib" / "src" / "metta" / "runner" / "builtin_mods" / "das.metta",
        ),
        ("he-many-forms", "he", generated["many-forms"]),
        ("he-wide-form", "he", generated["wide-form"]),
        ("he-deep-forms", "he", generated["deep-forms"]),
        ("petta-greedy-chess", "petta", petta_root / "examples" / "greedy_chess.metta"),
        ("petta-nilbc", "petta", petta_root / "examples" / "nilbc.metta"),
        ("petta-lib-pln", "petta", petta_root / "lib" / "lib_pln.metta"),
        ("petta-many-forms", "petta", generated["many-forms"]),
        ("petta-wide-form", "petta", generated["wide-form"]),
        ("petta-deep-forms", "petta", generated["deep-forms"]),
    ]


def pin_authorities(he_root: Path, petta_root: Path) -> None:
    if he_authority_revision(he_root) != EXPECTED_HE_REVISION:
        raise GateFailure("HE parser authority revision changed")
    if he_authority_digest(he_root) != EXPECTED_HE_DIGEST:
        raise GateFailure("HE parser authority source changed")
    if petta_revision(petta_root) != EXPECTED_PETTA_REVISION:
        raise GateFailure("PeTTa parser authority revision changed")
    if file_digest(petta_root / "src" / "parser.pl") != EXPECTED_PARSER_DIGEST:
        raise GateFailure("PeTTa per-form parser authority changed")
    if file_digest(petta_root / "src" / "filereader.pl") != EXPECTED_READER_DIGEST:
        raise GateFailure("PeTTa document reader authority changed")


def parse_stream(raw: bytes, header: str) -> dict[str, str]:
    try:
        lines = raw.decode("utf-8").splitlines()
    except UnicodeDecodeError as error:
        raise GateFailure("parse-only helper emitted invalid UTF-8") from error
    if not lines or lines[0] != header or lines[-1] != "end":
        raise GateFailure(f"malformed {header} receipt framing")
    fields: dict[str, str] = {}
    for line in lines[1:-1]:
        name, separator, value = line.partition("\t")
        if not separator or not name or name in fields:
            raise GateFailure(f"malformed {header} receipt field")
        fields[name] = value
    return fields


def positive_int(fields: dict[str, str], name: str, *, allow_zero: bool = False) -> int:
    try:
        value = int(fields[name])
    except (KeyError, ValueError) as error:
        raise GateFailure(f"missing or invalid {name} receipt") from error
    if value < 0 or (value == 0 and not allow_zero):
        raise GateFailure(f"invalid {name} receipt")
    return value


def run_he(
    binary: Path,
    path: Path,
    warmup: int,
    iterations: int,
) -> dict[str, Any]:
    completed = run_checked(
        [str(binary), str(path), str(warmup), str(iterations)],
        cwd=ROOT,
    )
    fields = parse_stream(completed.stdout, "he-parser-parse-only-v1")
    semantic = fields.get("semantic-fnv64", "")
    if len(semantic) != 16 or any(char not in "0123456789abcdef" for char in semantic):
        raise GateFailure("invalid HE semantic fingerprint")
    return {
        "input_bytes": positive_int(fields, "input-bytes", allow_zero=True),
        "items": positive_int(fields, "top-level-atoms"),
        "nodes": positive_int(fields, "atom-nodes"),
        "semantic": semantic,
        "warmup": positive_int(fields, "warmup-iterations", allow_zero=True),
        "iterations": positive_int(fields, "measured-iterations"),
        "nanoseconds": positive_int(fields, "total-nanoseconds"),
    }


def run_petta(
    petta_root: Path,
    path: Path,
    warmup: int,
    iterations: int,
) -> dict[str, Any]:
    completed = run_checked(
        [
            "swipl",
            "-q",
            "-f",
            str(PETTA_SOURCE),
            "--",
            str(petta_root),
            str(path),
            str(warmup),
            str(iterations),
        ],
        cwd=petta_root,
    )
    fields = parse_stream(completed.stdout, "petta-parser-parse-only-v1")
    semantic = fields.get("semantic-sha256", "")
    if len(semantic) != 64 or any(char not in "0123456789abcdef" for char in semantic):
        raise GateFailure("invalid PeTTa semantic digest")
    return {
        "input_bytes": positive_int(fields, "input-bytes", allow_zero=True),
        "items": positive_int(fields, "forms"),
        "nodes": None,
        "semantic": semantic,
        "warmup": positive_int(fields, "warmup-iterations", allow_zero=True),
        "iterations": positive_int(fields, "measured-iterations"),
        "nanoseconds": positive_int(fields, "total-nanoseconds"),
    }


def run_case(
    dialect: str,
    he_binary: Path,
    petta_root: Path,
    path: Path,
    warmup: int,
    iterations: int,
) -> dict[str, Any]:
    if dialect == "he":
        result = run_he(he_binary, path, warmup, iterations)
    elif dialect == "petta":
        result = run_petta(petta_root, path, warmup, iterations)
    else:
        raise GateFailure(f"unknown stable parser dialect: {dialect}")
    source = path.read_bytes()
    if result["input_bytes"] != len(source):
        raise GateFailure(f"{path.name}: helper input-byte receipt changed")
    result["source_sha256"] = sha256(source).hexdigest()
    return result


def structural_record(
    label: str,
    dialect: str,
    result: dict[str, Any],
) -> dict[str, Any]:
    return {
        "dialect": dialect,
        "input_bytes": result["input_bytes"],
        "items": result["items"],
        "label": label,
        "nodes": result["nodes"],
        "semantic": result["semantic"],
        "source_sha256": result["source_sha256"],
    }


def records_digest(records: list[dict[str, Any]]) -> str:
    canonical = json.dumps(
        records,
        ensure_ascii=False,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")
    return sha256(canonical).hexdigest()


def benchmark_case(
    label: str,
    dialect: str,
    he_binary: Path,
    petta_root: Path,
    path: Path,
    probe: dict[str, Any],
    target_ms: int,
    samples: int,
) -> None:
    probe_ns = max(1, probe["nanoseconds"] // probe["iterations"])
    target_ns = target_ms * 1_000_000
    iterations = max(1, min(10_000, round(target_ns / probe_ns)))
    sample_values: list[int] = []
    for _ in range(samples):
        observed = run_case(
            dialect,
            he_binary,
            petta_root,
            path,
            2,
            iterations,
        )
        if structural_record(label, dialect, observed) != structural_record(label, dialect, probe):
            raise GateFailure(f"{label}: semantic receipt changed during timing samples")
        sample_values.append(observed["nanoseconds"] // iterations)
    median_ns = int(statistics.median(sample_values))
    if median_ns <= 0:
        raise GateFailure(f"{label}: timer resolution was insufficient")
    mib_per_second = (
        probe["input_bytes"] * 1_000_000_000 / median_ns / (1024 * 1024)
    )
    print(
        f"(StableParserParseOnlyV1Receipt {label} {dialect} "
        f"{probe['input_bytes']} {probe['items']} {iterations} {samples} "
        f"{median_ns} {mib_per_second:.3f} {probe['source_sha256']} "
        f"{probe['semantic']})"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--he-root", type=Path, required=True)
    parser.add_argument("--petta-root", type=Path, required=True)
    parser.add_argument("--benchmark", action="store_true")
    parser.add_argument("--target-ms", type=int, default=250)
    parser.add_argument("--samples", type=int, default=3)
    arguments = parser.parse_args()
    if arguments.target_ms <= 0 or arguments.samples <= 0:
        parser.error("target-ms and samples must be positive")

    he_root = arguments.he_root.resolve()
    petta_root = arguments.petta_root.resolve()
    pin_authorities(he_root, petta_root)
    if not HE_SOURCE.is_file() or not PETTA_SOURCE.is_file():
        raise GateFailure("parse-only helper source is missing")
    he_binary = build_he_helper(he_root)

    cases = corpus_cases(he_root, petta_root)
    records: list[dict[str, Any]] = []
    probes: list[tuple[str, str, Path, dict[str, Any]]] = []
    for label, dialect, path in cases:
        if not path.is_file():
            raise GateFailure(f"stable parser corpus is missing: {label}")
        observed = run_case(dialect, he_binary, petta_root, path, 1, 1)
        records.append(structural_record(label, dialect, observed))
        probes.append((label, dialect, path, observed))

    matrix_digest = records_digest(records)
    if matrix_digest != EXPECTED_MATRIX_DIGEST:
        raise GateFailure(
            "stable parser parse-only matrix changed: "
            f"expected {EXPECTED_MATRIX_DIGEST}, got {matrix_digest}"
        )
    if arguments.benchmark:
        for label, dialect, path, probe in probes:
            benchmark_case(
                label,
                dialect,
                he_binary,
                petta_root,
                path,
                probe,
                arguments.target_ms,
                arguments.samples,
            )
    print(
        f"(StableParserParseOnlyV1Summary {len(cases)} 6 6 3 3 "
        f"{matrix_digest} 0)"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except GateFailure as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
