#!/usr/bin/env python3
"""Shared workspace path discovery for migrated Mettapedia consumers."""

from __future__ import annotations

import os
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parents[1]


def _resolve_override(env_var: str) -> Path | None:
    override = os.environ.get(env_var)
    if not override:
        return None
    path = Path(override).expanduser().resolve()
    if path.is_dir():
        return path
    raise FileNotFoundError(f"{env_var} points to a missing directory: {path}")


def _resolve_first_existing(label: str, candidates: list[Path]) -> Path:
    for candidate in candidates:
        if candidate.is_dir():
            return candidate.resolve()
    joined = ", ".join(str(path) for path in candidates)
    raise FileNotFoundError(f"unable to locate {label}; tried: {joined}")


def discover_mettapedia_root() -> Path:
    override = _resolve_override("METTAPEDIA_ROOT")
    if override is not None:
        return override
    return _resolve_first_existing(
        "Mettapedia Lean root",
        [
            WORKSPACE / "Mettapedia" / "lean" / "mettapedia",
        ],
    )


def discover_algorithms_root() -> Path:
    for env_var in ("ALGORITHMS_ROOT", "METTAPEDIA_ALGORITHMS_ROOT"):
        override = _resolve_override(env_var)
        if override is not None:
            return override
    return _resolve_first_existing(
        "algorithms Lean root",
        [
            WORKSPACE / "Mettapedia" / "lean" / "algorithms",
        ],
    )


def workspace_display(path: Path) -> str:
    resolved = path.resolve()
    try:
        return str(resolved.relative_to(WORKSPACE)).replace("\\", "/")
    except ValueError:
        return str(resolved)
