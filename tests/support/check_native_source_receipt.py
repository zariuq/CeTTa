#!/usr/bin/env python3
"""Check a frozen binary's reconstructible source-snapshot receipt.

The saved patch is applied to HEAD in an isolated temporary Git index. Every
indexed ledger path must then have exactly its recorded bytes. Unindexed
ledger entries may be tests only; all runtime source and build files must be
reconstructible. This checks the supplied source/build record, not a
cryptographic linkage from ELF bytes back to compiler inputs.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

from check_native_observers import save_json, sha256
from native_observer_probes import REVIEWED_SHA256S


def acceptable_missing(paths):
    return all(path.startswith("tests/") for path in paths)


def required_build_paths(paths):
    return {path for path in paths if path == "Makefile" or
            path.startswith(("src/", "native/", "runtime/bootstrap/build_config."))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--patch", type=Path, required=True)
    parser.add_argument("--ledger", type=Path, required=True)
    parser.add_argument("--build-log", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    checker = Path(__file__).resolve()
    checker_identity = sha256(checker)
    identity = sha256(binary)
    if identity not in REVIEWED_SHA256S:
        raise SystemExit("no reviewed source-receipt profile for this executable")
    repo = args.repo.resolve(strict=True)
    patch = args.patch.resolve(strict=True)
    ledger_path = args.ledger.resolve(strict=True)
    build_log = args.build_log.resolve(strict=True)
    ledger = json.loads(ledger_path.read_text())
    if not ledger or not all(isinstance(path, str) and len(digest) == 64 for path, digest in ledger.items()):
        raise SystemExit("invalid source hash ledger")
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo, text=True).strip()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    with tempfile.TemporaryDirectory(dir=out) as temporary:
        environment = dict(os.environ, GIT_INDEX_FILE=str(Path(temporary) / "index"))
        subprocess.run(["git", "read-tree", "HEAD"], cwd=repo, env=environment,
                       check=True, capture_output=True)
        applied = subprocess.run(["git", "apply", "--cached", str(patch)], cwd=repo,
                                 env=environment, capture_output=True, text=True)
        if applied.returncode != 0:
            raise SystemExit("source patch does not apply to recorded HEAD: " + applied.stderr)
        indexed = set(subprocess.check_output(["git", "ls-files"], cwd=repo,
                                              env=environment, text=True).splitlines())
        covered = set(ledger) & indexed
        missing = sorted(set(ledger) - indexed)
        mismatches = []
        for path in sorted(covered):
            data = subprocess.check_output(["git", "show", ":" + path], cwd=repo, env=environment)
            digest = hashlib.sha256(data).hexdigest()
            if digest != ledger[path]:
                mismatches.append({"path": path, "expected": ledger[path], "actual": digest})
    required = required_build_paths(set(ledger))
    build_text = build_log.read_text(errors="replace")
    checks = {
        "binary_unchanged": sha256(binary) == identity,
        "checker_unchanged": sha256(checker) == checker_identity,
        "patch_applies_to_head": applied.returncode == 0,
        "indexed_bytes_match": not mismatches,
        "all_build_paths_reconstructible": required <= covered,
        "missing_are_tests_only": acceptable_missing(missing),
        "ledger_mostly_reconstructible": len(covered) * 100 >= len(ledger) * 99,
        "build_record_completed": ("PASS: registries keep exactly their holders" in build_text and
                                   "PASS: set spaces and inferred signatures answer alike" in build_text and
                                   "error:" not in build_text.lower()),
    }
    result = {"binary_sha256": identity, "head": head,
              "source_files": {checker.name: checker_identity},
              "patch_sha256": sha256(patch), "ledger_sha256": sha256(ledger_path),
              "build_log_sha256": sha256(build_log), "ledger_entries": len(ledger),
              "indexed_entries": len(covered), "unindexed_entries": missing,
              "required_build_paths": len(required), "mismatches": mismatches,
              "checks": checks, "status": "qualified-source-receipt" if all(checks.values()) else "invalid-source-receipt",
              "boundary": "Reconstructible source/build record; no cryptographic ELF-to-compiler-input linkage."}
    save_json(out / "source-receipt.json", result)
    print(json.dumps(result, indent=2))
    return 0 if result["status"] == "qualified-source-receipt" else 1


if __name__ == "__main__":
    raise SystemExit(main())
