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
from collections import Counter
from pathlib import Path
from typing import Any

from mettapedia_paths import (
    discover_algorithms_root,
    discover_mettapedia_root,
    workspace_display,
)

CONTRACT_DATE = "2026-06-07"

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parents[1]
OFFICIAL_SPEC = WORKSPACE / "tmp" / "he_metta_official_specs.md"
METTAPEDIA = discover_mettapedia_root()
ALGORITHMS = discover_algorithms_root()
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
    "profile_get_doc_formal_regression": ROOT / "tests" / "support" / "profile_get_doc_formal_surface.metta",
    "profile_get_doc_extended_regression": ROOT / "tests" / "support" / "profile_get_doc_extended_surface.metta",
}

MASK_PROFILES = {
    "CETTA_PROFILE_MASK_HE_FORMAL": ["he"],
    "CETTA_PROFILE_MASK_HE_BASE": ["he", "he-compat"],
    "CETTA_PROFILE_MASK_HE_COMPAT": ["he-compat"],
    "CETTA_PROFILE_MASK_HE_EXTENDED": ["he-extended"],
    "CETTA_PROFILE_MASK_HE_PRIME": ["he-prime"],
    "CETTA_PROFILE_MASK_HE_PUBLIC": ["he", "he-compat", "he-extended", "he-prime"],
    "CETTA_PROFILE_MASK_HE_EXTENDED_PLUS": ["he-extended", "he-prime"],
    "CETTA_PROFILE_MASK_HE_NON_COMPAT": ["he", "he-extended", "he-prime"],
    "CETTA_PROFILE_MASK_ALL": ["he", "he-compat", "he-extended", "he-prime"],
}

OSLF_ANCHORS = {
    "language_def": (
        "Mettapedia/lean/mettapedia/Mettapedia/Languages/MeTTa/HE/"
        "HELanguageDef.lean:mettaHE"
    ),
    "type_synthesis": (
        "Mettapedia/lean/mettapedia/Mettapedia/OSLF/Framework/"
        "TypeSynthesis.lean:langDiamondUsing/langBoxUsing/langGaloisUsing"
    ),
    "premise_reduction": (
        "Mettapedia/lean/mettapedia/Mettapedia/OSLF/MeTTaIL/"
        "DeclReducesWithPremises.lean"
    ),
}


def file_sha256(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {"path": workspace_display(path), "exists": False}
    data = path.read_bytes()
    return {
        "path": workspace_display(path),
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
            "path": workspace_display(repo),
            "head": run(["rev-parse", "HEAD"]),
            "dirty": bool(run(["status", "--short"])),
        }
    except Exception:  # noqa: BLE001
        return {"path": workspace_display(repo), "head": "unknown", "dirty": "unknown"}


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
                "rust_he_compat_semantics": ".rust_he_compat_semantics = true" in body,
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
                "the active formal/extended HE language/profile; public HE "
                "surfaces remain documented, while extension-only surfaces are "
                "hidden from the formal HE base profile."
            ),
            "implementation_anchors": [
                "src/session.c:CETTA_SURFACE_POLICIES",
                "src/eval.c:__cetta_surface-available",
                "lib/stdlib.metta:get-doc",
            ],
            "regression_tests": [
                "tests/support/profile_get_doc_formal_surface.metta",
                "tests/support/profile_get_doc_extended_surface.metta",
            ],
            "spec_refs": [
                "he_metta_official_specs.md:profile/lang surfaces",
            ],
            "status": "implemented-and-regression-tested",
        },
        {
            "id": "he-compat-reproduces-rust-get-doc-arity",
            "statement": (
                "The he-compat lane reproduces upstream Hyperon Experimental "
                "0.2.10's single-argument get-doc arity error, even though the "
                "formal HE lane keeps profile-aware documentation."
            ),
            "implementation_anchors": [
                "src/session.c:CETTA_PROFILE_HE_COMPAT_VALUE",
                "src/eval.c:get-doc Rust compatibility branch",
            ],
            "regression_tests": [
                "tests/support/profile_get_doc_compat_surface.metta",
            ],
            "spec_refs": [
                "tests/generated/he_compat/he_compat_cases_2026-06-07.json:he_g1_docs",
            ],
            "status": "implemented-and-regression-tested",
        }
    ]


def premise_flow_contracts() -> list[dict[str, Any]]:
    return [
        {
            "id": "function-type-applicability-premise-flow",
            "statement": (
                "A function call may enter the typed function path only after "
                "the function type is applicable to the actual argument list "
                "and expected return type."
            ),
            "lean_language_def_refs": [
                "HELanguageDef.mettaHE:IE_FuncType",
                "HELanguageDef.mettaHE:applicableFuncType premise",
                "HELanguageDef.mettaHE:IF_AfterOp_EvalArgs",
            ],
            "oslf_refs": [OSLF_ANCHORS["language_def"], OSLF_ANCHORS["premise_reduction"]],
            "implementation_anchors": [
                "src/eval.c:type matching / function applicability paths",
                "src/eval.c:infer_dependent_application_types",
            ],
            "regression_tests": [
                "tests/test_function_type_check.metta",
                "tests/he_d4_type_prop.metta",
                "tests/he_d5_auto_types.metta",
            ],
            "status": "implemented-and-regression-tested",
        },
        {
            "id": "argument-evaluation-premise-flow",
            "statement": (
                "After the operator is evaluated, arguments are evaluated in "
                "the order and with the expected types dictated by the selected "
                "function type; Empty/Error short-circuit rather than becoming "
                "ordinary argument values."
            ),
            "lean_language_def_refs": [
                "HELanguageDef.mettaHE:IF_AfterOp_EvalArgs",
                "HELanguageDef.mettaHE:IA_Start_Typed",
                "HELanguageDef.mettaHE:IA_Head_Empty",
                "HELanguageDef.mettaHE:IA_Tail_Empty",
            ],
            "oslf_refs": [OSLF_ANCHORS["language_def"], OSLF_ANCHORS["type_synthesis"]],
            "implementation_anchors": [
                "src/eval.c:eval expression/function argument paths",
                "src/eval.c:result_set_filter_empty",
            ],
            "regression_tests": [
                "tests/he_c3_pln_stv.metta",
                "tests/test_he_frontier_algebra_regression.metta",
            ],
            "status": "implemented-and-regression-tested",
        },
        {
            "id": "match-premise-flow",
            "statement": (
                "match first parses the active-space query, then emits one "
                "successor for each space match and Empty for no-match."
            ),
            "lean_language_def_refs": [
                "HELanguageDef.mettaHE:MC_Match",
                "HELanguageDef.mettaHE:MC_Match_Empty",
            ],
            "oslf_refs": [OSLF_ANCHORS["premise_reduction"], OSLF_ANCHORS["type_synthesis"]],
            "implementation_anchors": [
                "src/eval.c:match builtins / space query path",
                "lib/lts/he.metta:lts:he:transitions",
            ],
            "regression_tests": [
                "tests/generated/he_contract/test_match.metta",
                "tests/test_match_atoms_bidir.metta",
            ],
            "status": "implemented-and-regression-tested",
        },
        {
            "id": "unify-premise-flow",
            "statement": (
                "unify has exactly the local-match success branch and the "
                "local-no-match failure branch, each re-entering evaluation "
                "with the selected continuation term."
            ),
            "lean_language_def_refs": [
                "HELanguageDef.mettaHE:MC_Unify_Match",
                "HELanguageDef.mettaHE:MC_Unify_NoMatch",
            ],
            "oslf_refs": [OSLF_ANCHORS["premise_reduction"], OSLF_ANCHORS["type_synthesis"]],
            "implementation_anchors": ["src/eval.c:unify minimal instruction path"],
            "regression_tests": ["tests/generated/he_contract/test_unify.metta"],
            "status": "implemented-and-regression-tested",
        },
        {
            "id": "chain-premise-flow",
            "statement": (
                "chain evaluates its input term, substitutes the result through "
                "the named variable in the continuation template, and continues "
                "from that continuation."
            ),
            "lean_language_def_refs": ["HELanguageDef.mettaHE:Metta", "HELanguageDef.mettaHE:R_Done"],
            "oslf_refs": [OSLF_ANCHORS["premise_reduction"]],
            "implementation_anchors": ["src/eval.c:chain minimal instruction path"],
            "regression_tests": [
                "tests/test_chain_nondet.metta",
                "tests/he_b1_equal_chain.metta",
            ],
            "status": "implemented-and-regression-tested",
        },
    ]


def successor_shape_contracts() -> list[dict[str, Any]]:
    return [
        {
            "id": "error-successor-shape",
            "statement": (
                "Error atoms propagate as visible Error successors and are not "
                "silently converted to Empty or ordinary values."
            ),
            "lean_language_def_refs": [
                "HELanguageDef.mettaHE:ErrorAtom",
                "HELanguageDef.mettaHE:M_Error",
                "HELanguageDef.mettaHE:MC_Error",
            ],
            "oslf_refs": [OSLF_ANCHORS["language_def"], OSLF_ANCHORS["type_synthesis"]],
            "implementation_anchors": [
                "src/eval.c:error propagation paths",
                "src/main.c:write_results",
            ],
            "regression_tests": [
                "tests/test_bad_type_error.metta",
                "tests/test_no_return_error.metta",
            ],
            "status": "implemented-and-regression-tested",
        },
        {
            "id": "lts-diamond-frontier-shape",
            "statement": (
                "The visible lts:he transition frontier is the implementation "
                "counterpart of OSLF ◇: zero successors for quiescent/Empty, "
                "one successor per visible nondeterministic branch, and Error "
                "propagation as an Error successor."
            ),
            "lean_language_def_refs": [
                "HELanguageDef.mettaHE:rewrites",
                "Mettapedia.OSLF.Framework.TypeSynthesis.langDiamondUsing",
            ],
            "oslf_refs": [OSLF_ANCHORS["type_synthesis"]],
            "implementation_anchors": [
                "lib/lts/he.metta:lts:he:transitions",
                "src/library.c:lts_he_transitions_frontier",
                "src/eval.c:outcome_set_normalize_visible_frontier",
            ],
            "regression_tests": ["tests/test_he_frontier_algebra_regression.metta"],
            "status": "implemented-and-regression-tested",
        },
        {
            "id": "match-successor-shape",
            "statement": (
                "match emits one successor per successful active-space match "
                "and emits Empty only for the no-match branch."
            ),
            "lean_language_def_refs": [
                "HELanguageDef.mettaHE:MC_Match",
                "HELanguageDef.mettaHE:MC_Match_Empty",
            ],
            "oslf_refs": [OSLF_ANCHORS["premise_reduction"], OSLF_ANCHORS["type_synthesis"]],
            "implementation_anchors": [
                "src/eval.c:match minimal instruction path",
                "lib/lts/he.metta:lts:he:transitions",
            ],
            "regression_tests": [
                "tests/generated/he_contract/test_match.metta",
                "tests/test_match_atoms_bidir.metta",
            ],
            "status": "implemented-and-regression-tested",
        },
        {
            "id": "unify-successor-shape",
            "statement": (
                "unify emits the success continuation on local match and the "
                "failure continuation on local no-match."
            ),
            "lean_language_def_refs": [
                "HELanguageDef.mettaHE:MC_Unify_Match",
                "HELanguageDef.mettaHE:MC_Unify_NoMatch",
            ],
            "oslf_refs": [OSLF_ANCHORS["premise_reduction"], OSLF_ANCHORS["type_synthesis"]],
            "implementation_anchors": ["src/eval.c:unify minimal instruction path"],
            "regression_tests": ["tests/generated/he_contract/test_unify.metta"],
            "status": "implemented-and-regression-tested",
        },
        {
            "id": "chain-successor-shape",
            "statement": (
                "chain emits successors obtained by substituting each evaluated "
                "input result into the continuation template."
            ),
            "lean_language_def_refs": ["HELanguageDef.mettaHE:Metta", "HELanguageDef.mettaHE:R_Done"],
            "oslf_refs": [OSLF_ANCHORS["premise_reduction"], OSLF_ANCHORS["type_synthesis"]],
            "implementation_anchors": ["src/eval.c:chain minimal instruction path"],
            "regression_tests": [
                "tests/test_chain_nondet.metta",
                "tests/he_b1_equal_chain.metta",
            ],
            "status": "implemented-and-regression-tested",
        },
        {
            "id": "equation-query-successor-shape",
            "statement": (
                "Each equation query result becomes a successor that re-enters "
                "metta evaluation at the substituted right-hand side; no-match "
                "returns the original call unchanged."
            ),
            "lean_language_def_refs": ["HELanguageDef.mettaHE:MC_Equation", "HELanguageDef.mettaHE:MC_NoMatch"],
            "oslf_refs": [OSLF_ANCHORS["premise_reduction"], OSLF_ANCHORS["type_synthesis"]],
            "implementation_anchors": ["src/eval.c:equation application path"],
            "regression_tests": [
                "tests/io_he_simple_rewrite.metta",
                "tests/io_he_unknown_expr_preserved.metta",
                "tests/he_c3_pln_stv.metta",
            ],
            "status": "implemented-and-regression-tested",
        },
        {
            "id": "eval-successor-shape",
            "statement": (
                "formal HE eval of an unreduced expression surfaces the inner "
                "expression, while he-compat surfaces Rust HE 0.2.10's wrapped "
                "eval expression."
            ),
            "lean_language_def_refs": ["HELanguageDef.mettaHE:MC_Grounded", "HELanguageDef.mettaHE:MC_NoMatch"],
            "oslf_refs": [OSLF_ANCHORS["language_def"]],
            "implementation_anchors": [
                "src/eval.c:eval compatibility branch",
                "src/session.c:CETTA_PROFILE_HE_FORMAL_VALUE",
                "src/session.c:CETTA_PROFILE_HE_COMPAT_VALUE",
            ],
            "regression_tests": ["tests/test_eval_grounded.metta"],
            "status": "implemented-and-regression-tested",
        },
        {
            "id": "type-cast-successor-shape",
            "statement": (
                "TypeCast either returns the original atom on a type match or "
                "returns Error(atom, BadType(expected, actual)) on mismatch."
            ),
            "lean_language_def_refs": ["HELanguageDef.mettaHE:TC_Match", "HELanguageDef.mettaHE:TC_Mismatch"],
            "oslf_refs": [OSLF_ANCHORS["language_def"]],
            "implementation_anchors": ["src/eval.c:type cast path"],
            "regression_tests": [
                "tests/test_type_cast.metta",
                "tests/test_bad_type_error.metta",
            ],
            "status": "implemented-and-regression-tested",
        },
        {
            "id": "grounded-dispatch-successor-shape",
            "statement": (
                "A grounded/native result is treated as an opaque premise result "
                "and each returned atom re-enters metta evaluation with the "
                "caller expected type."
            ),
            "lean_language_def_refs": ["HELanguageDef.mettaHE:MC_Grounded"],
            "oslf_refs": [OSLF_ANCHORS["premise_reduction"]],
            "implementation_anchors": ["src/eval.c:grounded/native call dispatch"],
            "regression_tests": [
                "tests/test_float_ops.metta",
                "tests/test_string_ops.metta",
                "tests/test_eval_grounded.metta",
            ],
            "status": "implemented-and-regression-tested",
        },
        {
            "id": "function-return-successor-shape",
            "statement": (
                "formal HE reports NoReturn when a function body exits without "
                "return; he-compat reproduces Rust HE 0.2.10's fresh-result "
                "surface for the compatibility lane."
            ),
            "lean_language_def_refs": ["HELanguageDef.mettaHE:Return", "HELanguageDef.mettaHE:R_Done"],
            "oslf_refs": [OSLF_ANCHORS["language_def"]],
            "implementation_anchors": [
                "src/eval.c:function compatibility branch",
                "src/session.c:rust_he_compat_semantics",
            ],
            "regression_tests": ["tests/test_no_return_error.metta"],
            "status": "implemented-and-regression-tested",
        },
        {
            "id": "superpose-successor-shape",
            "statement": "superpose emits one successor per tuple element and Empty for the empty tuple.",
            "lean_language_def_refs": [
                "HELanguageDef.mettaHE:MC_Superpose",
                "HELanguageDef.mettaHE:MC_Superpose_Empty",
            ],
            "oslf_refs": [OSLF_ANCHORS["type_synthesis"]],
            "implementation_anchors": ["src/eval.c:superpose minimal instruction path"],
            "regression_tests": [
                "tests/generated/he_contract/test_superpose_collapse.metta",
                "tests/test_he_frontier_algebra_regression.metta",
            ],
            "status": "implemented-and-regression-tested",
        },
        {
            "id": "collapse-successor-shape",
            "statement": "collapse collects the terminal visible results of the nested evaluation frontier.",
            "lean_language_def_refs": ["HELanguageDef.mettaHE:MC_Collapse"],
            "oslf_refs": [OSLF_ANCHORS["type_synthesis"]],
            "implementation_anchors": ["src/eval.c:collapse minimal instruction path"],
            "regression_tests": ["tests/generated/he_contract/test_superpose_collapse.metta"],
            "status": "implemented-and-regression-tested",
        },
        {
            "id": "context-space-successor-shape",
            "statement": "context-space and evalc use the active interpreter space as the successor context.",
            "lean_language_def_refs": ["HELanguageDef.mettaHE:State", "HELanguageDef.mettaHE:Space"],
            "oslf_refs": [OSLF_ANCHORS["language_def"]],
            "implementation_anchors": ["src/eval.c:context-space/evalc minimal instruction paths"],
            "regression_tests": [
                "tests/test_context_space.metta",
                "tests/test_evalc.metta",
            ],
            "status": "implemented-and-regression-tested",
        },
    ]


def arity_contracts() -> list[dict[str, Any]]:
    return [
        {
            "id": "constructor-arity-surface",
            "statement": (
                "Native surface declarations expose arity and result shape for "
                "structural constructors and lts:he helpers; future language "
                "surfaces should extend this table rather than hard-code arity "
                "checks per language."
            ),
            "implementation_anchors": [
                "lib/lts.metta",
                "lib/lts/he.metta",
                "src/session.c:CETTA_SURFACE_POLICIES",
            ],
            "regression_tests": [
                "tests/generated/he_contract/test_cons_decons.metta",
                "tests/test_he_frontier_algebra_regression.metta",
            ],
            "status": "implemented-and-regression-tested",
        }
    ]


def profile_surface_contracts() -> list[dict[str, Any]]:
    return [
        {
            "id": "profile-he-formal-vs-rust-compat-split",
            "statement": (
                "--profile he is the formal/spec-faithful HE lane used by "
                "OSLF/NTT/DTT work; --profile he-compat is the literal Rust HE "
                "0.2.10 compatibility lane."
            ),
            "implementation_anchors": [
                "src/session.c:CETTA_PROFILE_HE_FORMAL_VALUE",
                "src/session.c:CETTA_PROFILE_HE_COMPAT_VALUE",
                "src/session.h:rust_he_compat_semantics",
            ],
            "regression_tests": [
                "tests/test_eval_grounded.metta",
                "tests/test_no_return_error.metta",
                "tests/he_g1_docs.metta",
                "tests/support/profile_get_doc_compat_surface.metta",
                "tests/support/profile_get_doc_formal_surface.metta",
            ],
            "status": "implemented-and-regression-tested",
        },
        {
            "id": "profile-surface-policy-table",
            "statement": (
                "Language/profile surface availability is recorded in one "
                "explicit inventory so future --lang petta and --lang pure "
                "surfaces can be compared and tested without forking ad hoc "
                "checks."
            ),
            "implementation_anchors": [
                "src/session.c:CETTA_SURFACE_POLICIES",
                "src/session.c:cetta_surface_available",
                "src/eval.c:__cetta_surface-available",
            ],
            "regression_tests": [
                "tests/support/profile_get_doc_formal_surface.metta",
                "tests/support/profile_get_doc_extended_surface.metta",
            ],
            "status": "implemented-and-regression-tested",
        },
    ]


def module_policy_rows(catalog_path: Path) -> dict[str, list[dict[str, Any]]]:
    if not catalog_path.exists():
        return {"module_resolution": [], "module_inventory": []}
    try:
        data = json.loads(catalog_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return {"module_resolution": [], "module_inventory": []}
    triage = data.get("broad_probe", {}).get("triage", [])
    if not isinstance(triage, list):
        return {"module_resolution": [], "module_inventory": []}

    rows: dict[str, list[dict[str, Any]]] = {
        "module_resolution": [],
        "module_inventory": [],
    }
    for entry in triage:
        if not isinstance(entry, dict):
            continue
        category = str(entry.get("category", ""))
        if category == "module-resolution-diff":
            rows["module_resolution"].append(
                {
                    "path": entry.get("path", ""),
                    "classification": entry.get("classification", ""),
                    "category": category,
                    "policy_note": entry.get("module_resolution_note", ""),
                    "spec_refs": entry.get("module_resolution_spec_refs", []),
                    "spec_rule_ids": entry.get("spec_rule_ids", []),
                    "recommended_action": entry.get("recommended_action", ""),
                }
            )
        elif category == "module-inventory-environment-diff":
            rows["module_inventory"].append(
                {
                    "path": entry.get("path", ""),
                    "classification": entry.get("classification", ""),
                    "category": category,
                    "policy_note": entry.get("module_inventory_note", ""),
                    "spec_refs": entry.get("module_inventory_spec_refs", []),
                    "spec_rule_ids": entry.get("spec_rule_ids", []),
                    "recommended_action": entry.get("recommended_action", ""),
                }
            )

    for group in rows.values():
        group.sort(key=lambda row: str(row.get("path", "")))
    return rows


def module_policy_contracts(catalog_path: Path) -> list[dict[str, Any]]:
    rows = module_policy_rows(catalog_path)
    module_resolution = rows["module_resolution"]
    module_inventory = rows["module_inventory"]
    return [
        {
            "id": "module-import-policy-evidence-shape",
            "statement": (
                "Module-resolution rows are explicit policy evidence, not "
                "core he-compat evaluator obligations: Rust HE rejects these "
                "local-path or nested module names before shared semantics are "
                "reachable, while formal he owns the local-path, nested-import, "
                "cycle, parse-failure, and transactional rollback contract to "
                "pin next."
            ),
            "he_compat_obligation": (
                "Keep these rows out of core evaluator agreement counts unless "
                "Rust becomes runnable past the import/module-name boundary."
            ),
            "formal_he_obligation": (
                "Pin CeTTa's local-path import, nested import, cycle detection, "
                "parse-failure reporting, and transactional rollback semantics "
                "as formal he module/import behavior."
            ),
            "implementation_anchors": [
                "scripts/build_he_compat_catalog.py:BROAD_MODULE_RESOLUTION_NOTES",
                "scripts/build_he_compat_catalog.py:module-resolution rows require explicit policy notes",
                "src/library.c:cetta_library_include_module",
                "lib/stdlib.metta:@doc import!",
                "lib/stdlib.metta:@doc include",
            ],
            "catalog_category": "module-resolution-diff",
            "expected_policy_row_count": 6,
            "policy_rows": module_resolution,
            "regression_tests": [row["path"] for row in module_resolution],
            "spec_refs": sorted(
                {
                    str(ref)
                    for row in module_resolution
                    for ref in row.get("spec_refs", [])
                }
            ),
            "status": "policy-pinned-generated-evidence",
        },
        {
            "id": "module-inventory-policy-evidence-shape",
            "statement": (
                "Module-inventory rows are explicit policy evidence, not core "
                "he-compat evaluator obligations: Rust HE prints host/builtin "
                "module inventory while CeTTa prints the explicitly loaded "
                "session graph. Formal he should pin the CeTTa inventory "
                "contract before this becomes a core semantic obligation."
            ),
            "he_compat_obligation": (
                "Keep host-specific builtin inventory differences out of core "
                "evaluator agreement counts."
            ),
            "formal_he_obligation": (
                "Pin the module-inventory visible tree for explicitly loaded "
                "modules, including ordering and builtin-module visibility."
            ),
            "implementation_anchors": [
                "scripts/build_he_compat_catalog.py:BROAD_MODULE_INVENTORY_NOTES",
                "scripts/build_he_compat_catalog.py:module-inventory rows require explicit policy notes",
                "tests/spec_print_mods_inventory.metta",
            ],
            "catalog_category": "module-inventory-environment-diff",
            "expected_policy_row_count": 1,
            "policy_rows": module_inventory,
            "regression_tests": [row["path"] for row in module_inventory],
            "spec_refs": sorted(
                {
                    str(ref)
                    for row in module_inventory
                    for ref in row.get("spec_refs", [])
                }
            ),
            "status": "policy-pinned-generated-evidence",
        },
    ]


def catalog_rule_coverage(catalog_path: Path) -> dict[str, Any]:
    if not catalog_path.exists():
        return {"exists": False}
    try:
        data = json.loads(catalog_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        return {"exists": True, "error": str(exc)}
    summary = data.get("summary", {})
    capacities = data.get("spec_rule_capacities", [])
    by_spec_rule = summary.get("by_spec_rule", {})
    rule_status_counts: Counter[str] = Counter()
    rule_lane_counts: Counter[str] = Counter()
    mapped_case_count_by_lane: Counter[str] = Counter()
    capacity_rows: list[dict[str, Any]] = []
    uncovered_rule_ids: list[str] = []
    for row in capacities:
        if not isinstance(row, dict):
            continue
        rule_id = str(row.get("id", ""))
        mapped_case_count = int(by_spec_rule.get(rule_id, 0))
        status = str(row.get("status", ""))
        lane = str(row.get("track_a_relation_lane", "supporting-surface"))
        rule_status_counts[status] += 1
        rule_lane_counts[lane] += 1
        mapped_case_count_by_lane[lane] += mapped_case_count
        if mapped_case_count == 0:
            uncovered_rule_ids.append(rule_id)
        capacity_rows.append(
            {
                "id": rule_id,
                "title": row.get("title", ""),
                "status": status,
                "track_a_relation_lane": lane,
                "mapped_case_count": mapped_case_count,
                "lean_language_def_refs": row.get("lean_language_def_refs", []),
                "lean_authority_refs": row.get("lean_authority_refs", []),
                "oslf_refs": row.get("oslf_refs", []),
                "native_contract_ids": row.get("native_contract_ids", []),
                "alignment_note": row.get("alignment_note", ""),
                "authority_note": row.get("authority_note", ""),
            }
        )
    return {
        "exists": True,
        "catalog_date": data.get("catalog_date", ""),
        "case_count": summary.get("total", 0),
        "mapped_case_count": summary.get("mapped_case_count", 0),
        "covered_spec_rule_capacity_count": summary.get("covered_spec_rule_capacity_count", 0),
        "total_spec_rule_capacity_count": summary.get("total_spec_rule_capacity_count", len(capacities)),
        "by_spec_rule": by_spec_rule,
        "by_track_a_relation_lane": summary.get("by_track_a_relation_lane", {}),
        "uncovered_spec_rule_capacities": uncovered_rule_ids,
        "rule_lane_counts": dict(sorted(rule_lane_counts.items())),
        "mapped_case_count_by_lane": dict(sorted(mapped_case_count_by_lane.items())),
        "rule_status_counts": dict(sorted(rule_status_counts.items())),
        "capacity_rows": capacity_rows,
    }


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
            "mettapedia": git_head(METTAPEDIA),
            "algorithms": git_head(ALGORITHMS),
            "hyperon_experimental": git_head(WORKSPACE / "hyperon" / "hyperon-experimental"),
        },
        "provenance": {
            key: file_sha256(path) for key, path in sorted(source_paths.items())
        },
        "contracts": {
            "result_algebra": result_algebra_contracts(),
            "profile_doc_surface": profile_doc_surface_contracts(),
            "premise_flow": premise_flow_contracts(),
            "allowed_successor_shape": successor_shape_contracts(),
            "profile_surface": profile_surface_contracts(),
            "module_policy": module_policy_contracts(catalog_path),
            "arity_surface": {
                "contracts": arity_contracts(),
                "lts_declarations": [
                    *parse_lts_declarations(lts_generic, "lts"),
                    *parse_lts_declarations(lts_he, "lts:he"),
                ],
                "profile_surface_policies": parse_surface_policies(session_c),
                "profiles": parse_profiles(session_c),
            },
        },
        "rule_coverage": catalog_rule_coverage(catalog_path),
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
