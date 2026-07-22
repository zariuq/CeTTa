#!/usr/bin/env python3
"""Build and run the pinned HE parser oracle without modifying its checkout."""

from __future__ import annotations

import argparse
from hashlib import sha256
import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "tools" / "he_parser_oracle_v1.rs"
BUILD_ROOT = ROOT / "runtime" / "bootstrap" / "he_parser_oracle_v1"


class OracleFailure(RuntimeError):
    pass


def run_checked(command: list[str], *, cwd: Path) -> subprocess.CompletedProcess[bytes]:
    completed = subprocess.run(
        command,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        timeout=300,
    )
    if completed.returncode != 0:
        raise OracleFailure(
            f"command failed ({completed.returncode}): {' '.join(command)}\n"
            + completed.stderr.decode("utf-8", errors="replace")
        )
    return completed


def authority_revision(he_root: Path) -> str:
    completed = run_checked(
        ["git", "rev-parse", "HEAD"],
        cwd=he_root,
    )
    return completed.stdout.decode("ascii").strip()


def authority_digest(he_root: Path) -> str:
    parser_source = he_root / "lib" / "src" / "metta" / "text.rs"
    if not parser_source.is_file():
        raise OracleFailure("HE parser authority source is missing")
    return sha256(parser_source.read_bytes()).hexdigest()


def manifest_text(he_root: Path) -> str:
    hyperon_path = json.dumps(str(he_root / "lib"))
    atom_path = json.dumps(str(he_root / "hyperon-atom"))
    return f"""[package]
name = "he-parser-oracle-v1"
version = "0.0.0"
edition = "2021"

[dependencies]
hyperon = {{ path = {hyperon_path}, default-features = false, features = ["pkg_mgmt"] }}
hyperon-atom = {{ path = {atom_path} }}

[[bin]]
name = "he-parser-oracle-v1"
path = "src/main.rs"
"""


def prepare_build(he_root: Path) -> Path:
    source_dir = BUILD_ROOT / "src"
    source_dir.mkdir(parents=True, exist_ok=True)
    manifest = BUILD_ROOT / "Cargo.toml"
    generated_source = source_dir / "main.rs"
    expected_manifest = manifest_text(he_root)
    expected_source = SOURCE.read_bytes()
    if not manifest.exists() or manifest.read_text(encoding="utf-8") != expected_manifest:
        manifest.write_text(expected_manifest, encoding="utf-8")
    if not generated_source.exists() or generated_source.read_bytes() != expected_source:
        generated_source.write_bytes(expected_source)
    return manifest


def build_oracle(he_root: Path) -> Path:
    manifest = prepare_build(he_root)
    run_checked(
        [
            "cargo",
            "build",
            "--offline",
            "--quiet",
            "--manifest-path",
            str(manifest),
        ],
        cwd=BUILD_ROOT,
    )
    binary = BUILD_ROOT / "target" / "debug" / "he-parser-oracle-v1"
    if not binary.is_file():
        raise OracleFailure("Cargo succeeded without producing the HE parser oracle")
    return binary


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--he-root", type=Path, required=True)
    parser.add_argument("--expected-revision")
    parser.add_argument("--mode", choices=("atoms", "syntax"), required=True)
    parser.add_argument("--input", type=Path, required=True)
    arguments = parser.parse_args()

    he_root = arguments.he_root.resolve()
    input_path = arguments.input.resolve()
    revision = authority_revision(he_root)
    if arguments.expected_revision and revision != arguments.expected_revision:
        raise OracleFailure(
            f"HE authority revision mismatch: expected {arguments.expected_revision}, got {revision}"
        )
    binary = build_oracle(he_root)
    completed = run_checked(
        [str(binary), arguments.mode, str(input_path)],
        cwd=ROOT,
    )
    output = completed.stdout
    if not output.startswith(b"he-parser-oracle-v1\n") or not output.endswith(b"end\n"):
        raise OracleFailure("HE parser oracle emitted a malformed stream")
    sys.stdout.buffer.write(
        f"authority-revision\t{revision}\n".encode("ascii")
    )
    sys.stdout.buffer.write(
        f"authority-source-digest\t{authority_digest(he_root)}\n".encode("ascii")
    )
    sys.stdout.buffer.write(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
