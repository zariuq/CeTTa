#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import os
import platform
import subprocess
from datetime import datetime, timezone
from pathlib import Path


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


def benchmark_provenance(bin_path: str) -> dict[str, object]:
    binary = Path(bin_path).resolve()
    repo_root = _git_root(binary.parent) or Path(__file__).resolve().parent.parent
    commit = _git(repo_root, "rev-parse", "HEAD") or "unknown"
    status = _git(repo_root, "status", "--porcelain")
    inside = _git(repo_root, "rev-parse", "--is-inside-work-tree") == "true"
    tree_state = "dirty" if status else ("clean" if inside else "nogit")
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
