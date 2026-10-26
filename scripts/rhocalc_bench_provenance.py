#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import os
import platform
import subprocess
from datetime import datetime, timezone
from pathlib import Path

from cetta_readiness_model import load_frozen_baseline, ReadinessModelError


def _git(repo_root: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(repo_root), *args],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
    )
    return result.stdout.strip() if result.returncode == 0 else ""


def _git_root(path: Path) -> Path | None:
    root = _git(path, "rev-parse", "--show-toplevel")
    return Path(root) if root else None


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as handle:
            for block in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(block)
    except OSError:
        return "unknown"
    return digest.hexdigest()


def _cpu_model() -> str:
    try:
        for line in Path("/proc/cpuinfo").read_text(encoding="utf-8").splitlines():
            if line.lower().startswith("model name") and ":" in line:
                return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return platform.processor() or "unknown"


def benchmark_provenance(
    bin_path: str, *, receipt: str | None = None,
    receipt_sha256: str | None = None, source_commit: str | None = None,
) -> dict[str, object]:
    binary = Path(bin_path).resolve()
    repo_root = _git_root(binary.parent)
    if repo_root is not None and not all((repo_root / name).is_file() for name in (
            "src/eval.c", "src/main.c", "src/atom.c")):
        repo_root = None
    commit, tree_state = "unknown", "nogit"
    if any(value is not None for value in (receipt, receipt_sha256, source_commit)):
        if not all((receipt, receipt_sha256, source_commit)):
            raise ReadinessModelError("frozen provenance requires receipt, digest and commit")
        frozen = load_frozen_baseline(Path(__file__).resolve().parent.parent,
            Path(receipt), receipt_sha256, source_commit)
        if Path(frozen["binary"]) != binary:
            raise ReadinessModelError("frozen provenance names a different binary")
        commit, tree_state = frozen["commit"], "frozen"
    elif repo_root is not None:
        commit = _git(repo_root, "rev-parse", "HEAD") or "unknown"
        tree_state = "dirty" if _git(repo_root, "status", "--porcelain") else "clean"
    try:
        load1, load5, load15 = os.getloadavg()
    except OSError:
        load1 = load5 = load15 = -1.0
    return {
        "timestamp_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "commit": commit,
        "tree_state": tree_state,
        "binary_sha256": _sha256(binary),
        "platform": platform.platform(),
        "cpu_model": _cpu_model(),
        "logical_cpus": os.cpu_count() or 0,
        "load1": f"{load1:.3f}",
        "load5": f"{load5:.3f}",
        "load15": f"{load15:.3f}",
    }


PROVENANCE_FIELDS = [
    "timestamp_utc",
    "commit",
    "tree_state",
    "binary_sha256",
    "platform",
    "cpu_model",
    "logical_cpus",
    "load1",
    "load5",
    "load15",
]
