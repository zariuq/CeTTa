#!/usr/bin/env python3
"""Build the dated HE-compat semantic conformance catalog.

The catalog is an inventory and gate input, not a runtime feature.  It records
which existing CeTTa tests are HE-compatible candidates and which subset already
has independent upstream/Lean oracle coverage through the Mettapedia HE I/O
fixtures.
"""

from __future__ import annotations

import json
import hashlib
import subprocess
from pathlib import Path
from typing import Any


CATALOG_DATE = "2026-06-07"

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parents[1]
METTAPEDIA = WORKSPACE / "lean-projects" / "mettapedia"
FIXTURES = METTAPEDIA / "scripts" / "conformance" / "he_io_fixtures.json"
DEFAULT_OUT = (
    ROOT
    / "tests"
    / "generated"
    / "he_compat"
    / f"he_compat_cases_{CATALOG_DATE}.json"
)

DIRECT_PROBE_METHOD = (
    "exact-output probe of CeTTa --profile he-compat against upstream HE 0.2.10 "
    "on 2026-06-06"
)

DIRECT_PROBE_AGREE_PATHS = {
    "tests/generated/he_contract/test_assert.metta",
    "tests/generated/he_contract/test_case_switch.metta",
    "tests/generated/he_contract/test_cons_decons.metta",
    "tests/generated/he_contract/test_eval.metta",
    "tests/generated/he_contract/test_function_return.metta",
    "tests/generated/he_contract/test_match.metta",
    "tests/generated/he_contract/test_superpose_collapse.metta",
    "tests/generated/he_contract/test_unify.metta",
    "tests/he_a1_symbols.metta",
    "tests/he_a2_opencoggy.metta",
    "tests/he_a3_twoside.metta",
    "tests/he_b0_chaining_prelim.metta",
    "tests/he_b1_equal_chain.metta",
    "tests/he_b2_backchain.metta",
    "tests/he_b3_direct.metta",
    "tests/he_b4_nondeterm.metta",
    "tests/he_b5_types_prelim.metta",
    "tests/he_c1_grounded_basic.metta",
    "tests/he_c2_spaces_kb.metta",
    "tests/he_c3_pln_stv.metta",
    "tests/he_d1_gadt.metta",
    "tests/he_d2_higherfunc.metta",
    "tests/he_d3_deptypes.metta",
    "tests/he_d4_type_prop.metta",
    "tests/he_d5_auto_types.metta",
    "tests/he_e1_kb_write.metta",
    "tests/he_e2_states.metta",
    "tests/he_e3_match_states.metta",
    "tests/test_bad_type_error.metta",
    "tests/test_chain_nondet.metta",
    "tests/test_context_space.metta",
    "tests/test_empty_handling.metta",
    "tests/test_evalc.metta",
    "tests/test_float_ops.metta",
    "tests/test_function_type_check.metta",
    "tests/test_match_atoms_bidir.metta",
    "tests/test_meta_types.metta",
    "tests/test_nondeterministic_types.metta",
    "tests/test_string_ops.metta",
    "tests/test_type_cast.metta",
    "tests/test_variable_scoping.metta",
}

DIRECT_PROBE_DIVERGENCES = {
    "tests/he_g1_docs.metta": {
        "classification": "upstream-he-divergence",
        "notes": (
            "Formal HE follows the documented stdlib @doc/get-doc/help! surface "
            "for the active language/profile: get-doc returns @doc-formal data "
            "for surfaces available in that profile and hides unavailable "
            "extension surfaces. Upstream HE 0.2.10 fails the @doc-formal "
            "assertion with IncorrectNumberOfArguments."
        ),
    },
    "tests/test_eval_grounded.metta": {
        "classification": "upstream-he-divergence",
        "notes": (
            "Formal HE follows CeTTa's documented reading: when eval reaches a "
            "non-reducible expression E, the user-visible result is E itself. "
            "Upstream HE 0.2.10 instead leaves the wrapper as (eval E)."
        ),
    },
    "tests/test_no_return_error.metta": {
        "classification": "upstream-he-divergence",
        "notes": (
            "CeTTa returns (Error ... NoReturn) for a function block without "
            "return, matching minimal-metta.md function/return lines 134-136; "
            "upstream HE 0.2.10 returns a fresh variable."
        ),
    },
}


def rel(path: Path, base: Path = ROOT) -> str:
    try:
        return str(path.relative_to(base))
    except ValueError:
        return str(path)


def git_head(repo: Path) -> dict[str, Any]:
    def run(args: list[str]) -> str:
        return subprocess.check_output(
            ["git", "-C", str(repo), *args],
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()

    try:
        head = run(["rev-parse", "HEAD"])
        dirty = bool(run(["status", "--short"]))
        return {"path": str(repo), "head": head, "dirty": dirty}
    except Exception:  # noqa: BLE001
        return {"path": str(repo), "head": "unknown", "dirty": "unknown"}


def first_ref_line(path: Path) -> str:
    text = path.read_text(encoding="utf-8", errors="ignore")
    for line in text.splitlines():
        stripped = line.strip()
        if "Ref: metta.md" in stripped or "Ref: minimal-metta.md" in stripped:
            return stripped.lstrip(";").strip()
    return ""


def expected_path_for(metta_path: Path) -> Path:
    return metta_path.with_suffix(".expected")


def result_snapshot(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {}
    text = path.read_text(encoding="utf-8", errors="ignore")
    return {
        "kind": "expected-file",
        "path": rel(path),
        "sha256": hashlib.sha256(text.encode("utf-8")).hexdigest(),
        "bytes": len(text.encode("utf-8")),
        "text": text,
    }


def assertion_count(path: Path) -> int:
    return sum(
        1
        for line in path.read_text(encoding="utf-8", errors="ignore").splitlines()
        if line.lstrip().startswith("!(")
    )


def add_case(cases: list[dict[str, Any]], **row: Any) -> None:
    cases.append(row)


def direct_probe_row(rel_path: str) -> dict[str, Any]:
    if rel_path in DIRECT_PROBE_AGREE_PATHS:
        return {
            "classification": "agree",
            "comparison_evidence": {
                "method": DIRECT_PROBE_METHOD,
                "cetta_command": f"cetta --profile he-compat --lang he {rel_path}",
                "upstream_command": f"conda run -n hyperon metta {rel_path}",
            },
            "notes": "Direct CeTTa/upstream HE exact-output probe agreed.",
        }
    if rel_path in DIRECT_PROBE_DIVERGENCES:
        row = DIRECT_PROBE_DIVERGENCES[rel_path]
        return {
            "classification": row["classification"],
            "comparison_evidence": {
                "method": DIRECT_PROBE_METHOD,
                "cetta_command": f"cetta --profile he-compat --lang he {rel_path}",
                "upstream_command": f"conda run -n hyperon metta {rel_path}",
            },
            "notes": row["notes"],
        }
    return {
        "classification": "needs-triage",
        "comparison_evidence": {},
        "notes": "No direct upstream comparison has been recorded for this case.",
    }


def load_fixtures() -> list[dict[str, Any]]:
    if not FIXTURES.exists():
        return []
    data = json.loads(FIXTURES.read_text(encoding="utf-8"))
    if not isinstance(data, list):
        raise ValueError(f"fixture file must contain a JSON list: {FIXTURES}")
    return data


def build_cases() -> list[dict[str, Any]]:
    cases: list[dict[str, Any]] = []
    covered_cetta_paths: set[str] = set()

    for row in load_fixtures():
        fid = str(row["id"])
        tier_name = str(row.get("tier", ""))
        tier = 0 if tier_name == "core_anchor" else 1
        cetta_path = ROOT / "tests" / f"io_{fid}.metta"
        exp_path = expected_path_for(cetta_path)
        feature = str(row.get("implementation_group", "") or "core")
        lean_theorem = str(row.get("lean_theorem", "")).strip()
        expected = row.get("expected", [])
        if not isinstance(expected, list):
            expected = []
        covered_cetta_paths.add(rel(cetta_path))
        add_case(
            cases,
            id=f"mettapedia-he-io:{fid}",
            tier=tier,
            tier_name="tier0-core-anchor" if tier == 0 else "tier1-extension-target",
            source_kind="mettapedia_he_io_fixture",
            feature=feature,
            spec_refs=[str(row.get("source_note", "")).strip()],
            lean_authority=[lean_theorem] if lean_theorem else [],
            cetta_path=rel(cetta_path),
            cetta_expected_path=rel(exp_path) if exp_path.exists() else "",
            cetta_result=result_snapshot(exp_path),
            upstream_oracle={
                "engine": "Hyperon Experimental metta",
                "version": "0.2.10",
                "repository_head_ref": "repositories.hyperon_experimental.head",
                "baseline": str(
                    METTAPEDIA
                    / "scripts"
                    / "conformance"
                    / "he_io_baseline_hyperon_0.2.10.json"
                ),
                "fixture_id": fid,
            },
            lean_comparator={
                "engine": "simpleMeTTa he-run",
                "repository_head_ref": "repositories.algorithms.head",
                "authority_head_ref": "repositories.mettapedia.head",
                "required": bool(lean_theorem),
                "theorem_anchor": lean_theorem,
            },
            expected=[str(x) for x in expected],
            classification="agree",
            notes="Oracle-backed Mettapedia fixture; CeTTa gate reruns the local counterpart.",
        )

    for path in sorted((ROOT / "tests" / "generated" / "he_contract").glob("*.metta")):
        probe = direct_probe_row(rel(path))
        add_case(
            cases,
            id=f"generated-he-contract:{path.stem}",
            tier=2,
            tier_name="tier2-contract-assertion-corpus",
            source_kind="cetta_generated_he_contract",
            feature=path.stem.removeprefix("test_"),
            spec_refs=[path.read_text(encoding="utf-8", errors="ignore").splitlines()[0].lstrip(";").strip()],
            lean_authority=[],
            cetta_path=rel(path),
            cetta_expected_path=rel(expected_path_for(path)),
            cetta_result=result_snapshot(expected_path_for(path)),
            upstream_oracle={
                "engine": "Hyperon Experimental metta",
                "version": "0.2.10",
                "repository_head_ref": "repositories.hyperon_experimental.head",
                "mode": "direct-file-probe",
            },
            lean_comparator={},
            assertion_count=assertion_count(path),
            comparison_evidence=probe["comparison_evidence"],
            classification=probe["classification"],
            notes=(
                "Assertion-style HE contract case; sync path is repaired. "
                + probe["notes"]
            ),
        )

    for path in sorted((ROOT / "tests").glob("he_[a-g]*.metta")):
        probe = direct_probe_row(rel(path))
        add_case(
            cases,
            id=f"cetta-he-example:{path.stem}",
            tier=2,
            tier_name="tier2-historical-he-example",
            source_kind="cetta_he_example",
            feature="historical_he_ladder",
            spec_refs=[],
            lean_authority=[],
            cetta_path=rel(path),
            cetta_expected_path=rel(expected_path_for(path)),
            cetta_result=result_snapshot(expected_path_for(path)),
            upstream_oracle={
                "engine": "Hyperon Experimental metta",
                "version": "0.2.10",
                "repository_head_ref": "repositories.hyperon_experimental.head",
                "mode": "direct-file-probe",
            },
            lean_comparator={},
            comparison_evidence=probe["comparison_evidence"],
            classification=probe["classification"],
            notes="Historical HE example corpus. " + probe["notes"],
        )

    for path in sorted((ROOT / "tests").glob("*.metta")):
        if rel(path) in covered_cetta_paths:
            continue
        ref = first_ref_line(path)
        if not ref:
            continue
        probe = direct_probe_row(rel(path))
        add_case(
            cases,
            id=f"cetta-ref-test:{path.stem}",
            tier=2,
            tier_name="tier2-spec-referenced-test",
            source_kind="cetta_spec_referenced_test",
            feature="spec_reference",
            spec_refs=[ref],
            lean_authority=[],
            cetta_path=rel(path),
            cetta_expected_path=rel(expected_path_for(path)),
            cetta_result=result_snapshot(expected_path_for(path)),
            upstream_oracle={
                "engine": "Hyperon Experimental metta",
                "version": "0.2.10",
                "repository_head_ref": "repositories.hyperon_experimental.head",
                "mode": "direct-file-probe",
            },
            lean_comparator={},
            comparison_evidence=probe["comparison_evidence"],
            classification=probe["classification"],
            notes=(
                "Hand-written CeTTa test cites official HE/minimal MeTTa spec. "
                + probe["notes"]
            ),
        )

    return sorted(cases, key=lambda row: (row["tier"], row["source_kind"], row["id"]))


def summarize(cases: list[dict[str, Any]]) -> dict[str, Any]:
    by_tier: dict[str, int] = {}
    by_classification: dict[str, int] = {}
    by_source_kind: dict[str, int] = {}
    for row in cases:
        by_tier[str(row["tier"])] = by_tier.get(str(row["tier"]), 0) + 1
        cls = str(row["classification"])
        by_classification[cls] = by_classification.get(cls, 0) + 1
        kind = str(row["source_kind"])
        by_source_kind[kind] = by_source_kind.get(kind, 0) + 1
    return {
        "total": len(cases),
        "by_tier": by_tier,
        "by_classification": by_classification,
        "by_source_kind": by_source_kind,
        "tier0_1_needs_triage": sum(
            1
            for row in cases
            if int(row["tier"]) <= 1 and row["classification"] == "needs-triage"
        ),
    }


def main() -> int:
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()

    cases = build_cases()
    payload = {
        "schema_version": 1,
        "catalog_date": CATALOG_DATE,
        "subject": "CeTTa HE-compatible runtime surface",
        "no_runtime_trace_or_certificate_surface": True,
        "profile_policy": {
            "formal_he": {
                "scope": (
                    "Spec-faithful HE interpretation used as the formal base for "
                    "OSLF/NTT/DTT work."
                ),
                "eval_surface_sentence": (
                    "When the eval instruction reaches a non-reducible expression E, "
                    "the user-visible formal-HE result is E itself."
                ),
                "doc_surface_sentence": (
                    "get-doc and help! follow the documented stdlib documentation "
                    "surface for the active language/profile; unavailable "
                    "extension surfaces are hidden, and upstream HE 0.2.10 "
                    "get-doc arity failures on the HE documentation example are "
                    "treated as upstream divergence."
                ),
            },
            "he_compat": {
                "scope": (
                    "Compatibility catalog for upstream Hyperon Experimental HE 0.2.10. "
                    "Cases that intentionally follow the written/formal HE spec rather "
                    "than upstream quirks remain classified explicitly."
                ),
            },
        },
        "repositories": {
            "cetta": git_head(ROOT),
            "mettapedia": git_head(METTAPEDIA),
            "algorithms": git_head(WORKSPACE / "lean-projects" / "algorithms"),
            "hyperon_experimental": git_head(WORKSPACE / "hyperon" / "hyperon-experimental"),
        },
        "oracle_versions": {
            "upstream_hyperon_experimental": "metta-0.2.10",
            "lean_executable": "simpleMeTTa he-run (algorithms repo)",
        },
        "classification_values": [
            "agree",
            "cetta-bug",
            "lean-spec-bug",
            "official-spec-gap",
            "upstream-he-divergence",
            "intentional-cetta-divergence",
            "he-prime-only",
            "needs-triage",
        ],
        "summary": summarize(cases),
        "cases": cases,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"wrote {args.out} ({len(cases)} cases)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
