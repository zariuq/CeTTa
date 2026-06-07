#!/usr/bin/env python3
"""Build the dated HE native-contract ledger.

This artifact is a generated test/design ledger, not a runtime feature.  It
records the first native contracts that tie CeTTa's HE implementation surface to
the official HE spec snapshot, the lts:he frontier surface, and the current
profile surface table.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path
from typing import Any


CONTRACT_DATE = "2026-06-07"

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parents[1]
OFFICIAL_SPEC = WORKSPACE / "tmp" / "he_metta_official_specs.md"
HE_COMPAT_GENERATED_DIR = ROOT / "tests" / "generated" / "he_compat"
DEFAULT_CATALOG = HE_COMPAT_GENERATED_DIR / f"he_compat_cases_{CONTRACT_DATE}.json"
DEFAULT_OUT = HE_COMPAT_GENERATED_DIR / f"he_native_contracts_{CONTRACT_DATE}.json"

SOURCE_PATHS = {
    "official_he_spec_snapshot": OFFICIAL_SPEC,
    "cetta_eval": ROOT / "src" / "eval.c",
    "cetta_library": ROOT / "src" / "library.c",
    "cetta_cli": ROOT / "src" / "main.c",
    "cetta_session": ROOT / "src" / "session.c",
    "cetta_session_header": ROOT / "src" / "session.h",
    "symbol_table": ROOT / "src" / "symbol.h",
    "stdlib_surface": ROOT / "lib" / "stdlib.metta",
    "lts_generic_surface": ROOT / "lib" / "lts.metta",
    "lts_he_surface": ROOT / "lib" / "lts" / "he.metta",
    "he_compat_catalog": DEFAULT_CATALOG,
    "frontier_regression": ROOT / "tests" / "test_he_frontier_algebra_regression.metta",
    "he_c3_regression": ROOT / "tests" / "he_c3_pln_stv.metta",
    "he_d4_regression": ROOT / "tests" / "he_d4_type_prop.metta",
    "profile_get_doc_compat_regression": ROOT / "tests" / "support" / "profile_get_doc_compat_surface.metta",
    "profile_get_doc_extended_regression": ROOT / "tests" / "support" / "profile_get_doc_extended_surface.metta",
}

MASK_PROFILES = {
    "CETTA_PROFILE_MASK_HE_COMPAT": ["he-compat"],
    "CETTA_PROFILE_MASK_HE_EXTENDED": ["he-extended"],
    "CETTA_PROFILE_MASK_HE_PRIME": ["he-prime"],
    "CETTA_PROFILE_MASK_HE_PUBLIC": ["he-compat", "he-extended", "he-prime"],
    "CETTA_PROFILE_MASK_HE_EXTENDED_PLUS": ["he-extended", "he-prime"],
    "CETTA_PROFILE_MASK_ALL": ["he-compat", "he-extended", "he-prime"],
}


def file_sha256(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {"path": str(path), "exists": False}
    data = path.read_bytes()
    return {
        "path": str(path),
        "exists": True,
        "sha256": hashlib.sha256(data).hexdigest(),
        "bytes": len(data),
    }


def git_head(repo: Path) -> dict[str, Any]:
    def run(args: list[str]) -> str:
        return subprocess.check_output(
            ["git", "-C", str(repo), *args],
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()

    try:
        return {
            "path": str(repo),
            "head": run(["rev-parse", "HEAD"]),
            "dirty": bool(run(["status", "--short"])),
        }
    except Exception:  # noqa: BLE001
        return {"path": str(repo), "head": "unknown", "dirty": "unknown"}


def parse_profiles(session_c: str) -> list[dict[str, Any]]:
    pattern = re.compile(
        r"static const CettaProfile\s+[^=]+=\s*\{(?P<body>.*?)\};",
        re.DOTALL,
    )
    profiles: list[dict[str, Any]] = []
    for match in pattern.finditer(session_c):
        body = match.group("body")
        name = re.search(r'\.name\s*=\s*"([^"]+)"', body)
        note = re.search(r'\.note\s*=\s*"([^"]+)"', body)
        if not name:
            continue
        profiles.append(
            {
                "name": name.group(1),
                "note": note.group(1) if note else "",
                "he_compatible_surface": ".he_compatible_surface = true" in body,
                "enable_cetta_extensions": ".enable_cetta_extensions = true" in body,
                "enable_dependent_telescope": ".enable_dependent_telescope = true" in body,
            }
        )
    return profiles


def parse_surface_policies(session_c: str) -> list[dict[str, Any]]:
    table = re.search(
        r"static const CettaSurfacePolicy\s+CETTA_SURFACE_POLICIES\[\]\s*=\s*\{(?P<body>.*?)\};",
        session_c,
        re.DOTALL,
    )
    if not table:
        return []
    policies: list[dict[str, Any]] = []
    entry_pattern = re.compile(r'\{\s*"([^"]+)"\s*,\s*([A-Z0-9_]+)\s*,\s*"([^"]+)"\s*\}')
    for name, mask, rationale in entry_pattern.findall(table.group("body")):
        policies.append(
            {
                "surface": name,
                "profile_mask": mask,
                "profiles": MASK_PROFILES.get(mask, []),
                "rationale": rationale,
            }
        )
    return policies


def parse_lts_declarations(text: str, module: str) -> list[dict[str, Any]]:
    declarations: list[dict[str, Any]] = []
    pattern = re.compile(r"^\(:\s+([^\s]+)\s+\(->\s+(.+?)\)\)$")
    for line in text.splitlines():
        match = pattern.match(line.strip())
        if not match:
            continue
        name = match.group(1)
        tokens = match.group(2).split()
        if len(tokens) < 1:
            continue
        declarations.append(
            {
                "name": name,
                "module": module,
                "arity": max(len(tokens) - 1, 0),
                "argument_types": tokens[:-1],
                "return_type": tokens[-1],
            }
        )
    return declarations


def result_algebra_contracts() -> list[dict[str, Any]]:
    return [
        {
            "id": "empty-zero-visible-result-bag",
            "statement": (
                "Empty contributes no visible result when a non-Empty result "
                "survives in a finished HE result/frontier bag."
            ),
            "implementation_anchors": [
                "src/eval.c:result_set_filter_empty",
                "src/eval.c:outcome_set_normalize_visible_frontier",
                "src/main.c:write_results",
            ],
            "regression_tests": [
                "tests/test_he_frontier_algebra_regression.metta",
                "tests/he_d4_type_prop.metta",
            ],
            "spec_refs": [
                "he_metta_official_specs.md:minimal-metta Empty result algebra",
            ],
            "status": "implemented-and-regression-tested",
        },
        {
            "id": "empty-not-lts-successor",
            "statement": "Empty is not emitted as a quoted lts:he successor state.",
            "implementation_anchors": [
                "src/library.c:lts_he_transitions_frontier",
            ],
            "regression_tests": [
                "tests/test_he_frontier_algebra_regression.metta",
            ],
            "spec_refs": [
                "lib/lts.metta:quiescent states are reported by zero results",
                "lib/lts/he.metta:lts:he:transitions emits zero results on quiescent atoms",
            ],
            "status": "implemented-and-regression-tested",
        },
        {
            "id": "surviving-branch-reenters-evaluator",
            "statement": (
                "A surviving branch from an overlapping nondeterministic equation "
                "frontier is evaluated applicatively even when a sibling branch "
                "collapses to Empty."
            ),
            "implementation_anchors": [
                "src/eval.c:eval_delayed_outcome_for_caller",
                "src/eval.c:atom_is_constructor_normal_form",
            ],
            "regression_tests": [
                "tests/test_he_frontier_algebra_regression.metta",
                "tests/he_c3_pln_stv.metta",
            ],
            "spec_refs": [
                "he_metta_official_specs.md:minimal-metta applicative evaluation order",
            ],
            "status": "implemented-and-regression-tested",
        },
        {
            "id": "success-frontier-filters-errors",
            "statement": (
                "A visible successful frontier suppresses sibling Error outcomes; "
                "Empty alone does not count as success for that filter."
            ),
            "implementation_anchors": [
                "src/eval.c:outcome_set_filter_errors_if_success",
                "src/eval.c:outcome_set_normalize_visible_frontier",
            ],
            "regression_tests": [
                "tests/test_he_frontier_algebra_regression.metta",
            ],
            "spec_refs": [
                "he_metta_official_specs.md:minimal-metta result collection",
            ],
            "status": "implemented-and-regression-tested",
        },
    ]


def profile_doc_surface_contracts() -> list[dict[str, Any]]:
    return [
        {
            "id": "get-doc-respects-profile-surface",
            "statement": (
                "get-doc only returns documentation for surfaces available in "
                "the active HE language/profile; public HE surfaces remain "
                "documented, while extension-only surfaces are hidden from "
                "he-compat."
            ),
            "implementation_anchors": [
                "src/session.c:CETTA_SURFACE_POLICIES",
                "src/eval.c:__cetta_surface-available",
                "lib/stdlib.metta:get-doc",
            ],
            "regression_tests": [
                "tests/support/profile_get_doc_compat_surface.metta",
                "tests/support/profile_get_doc_extended_surface.metta",
            ],
            "spec_refs": [
                "he_metta_official_specs.md:profile/lang surfaces",
            ],
            "status": "implemented-and-regression-tested",
        }
    ]


def load_profile_policy(catalog_path: Path) -> dict[str, Any]:
    if not catalog_path.exists():
        return {}
    try:
        data = json.loads(catalog_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return {}
    policy = data.get("profile_policy", {})
    return policy if isinstance(policy, dict) else {}


def build_payload(catalog_path: Path) -> dict[str, Any]:
    session_c = SOURCE_PATHS["cetta_session"].read_text(encoding="utf-8", errors="ignore")
    lts_generic = SOURCE_PATHS["lts_generic_surface"].read_text(encoding="utf-8", errors="ignore")
    lts_he = SOURCE_PATHS["lts_he_surface"].read_text(encoding="utf-8", errors="ignore")

    source_paths = dict(SOURCE_PATHS)
    source_paths["he_compat_catalog"] = catalog_path

    return {
        "schema_version": 1,
        "contract_date": CONTRACT_DATE,
        "kind": "he-native-contract-ledger",
        "status": "generated-design-and-test-ledger-not-runtime-feature",
        "profile_policy": load_profile_policy(catalog_path),
        "repositories": {
            "cetta": git_head(ROOT),
            "mettapedia": git_head(WORKSPACE / "lean-projects" / "mettapedia"),
            "algorithms": git_head(WORKSPACE / "lean-projects" / "algorithms"),
            "hyperon_experimental": git_head(WORKSPACE / "hyperon" / "hyperon-experimental"),
        },
        "provenance": {
            key: file_sha256(path) for key, path in sorted(source_paths.items())
        },
        "contracts": {
            "result_algebra": result_algebra_contracts(),
            "profile_doc_surface": profile_doc_surface_contracts(),
            "arity_surface": {
                "lts_declarations": [
                    *parse_lts_declarations(lts_generic, "lts"),
                    *parse_lts_declarations(lts_he, "lts:he"),
                ],
                "profile_surface_policies": parse_surface_policies(session_c),
                "profiles": parse_profiles(session_c),
            },
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", type=Path, default=DEFAULT_CATALOG)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()

    payload = build_payload(args.catalog)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
