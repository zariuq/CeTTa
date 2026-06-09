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
import argparse
import re
import subprocess
from collections import Counter
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
    "tests/he_g1_docs.metta",
    "tests/test_bad_type_error.metta",
    "tests/test_chain_nondet.metta",
    "tests/test_context_space.metta",
    "tests/test_empty_handling.metta",
    "tests/test_eval_grounded.metta",
    "tests/test_evalc.metta",
    "tests/test_float_ops.metta",
    "tests/test_function_type_check.metta",
    "tests/test_match_atoms_bidir.metta",
    "tests/test_meta_types.metta",
    "tests/test_no_return_error.metta",
    "tests/test_nondeterministic_types.metta",
    "tests/test_string_ops.metta",
    "tests/test_type_cast.metta",
    "tests/test_variable_scoping.metta",
}

DIRECT_PROBE_DIVERGENCES: dict[str, dict[str, str]] = {}

BROAD_RUST_DRIFT_NOTES: dict[str, dict[str, Any]] = {
    "tests/test_width_tuple_semantics_regression.metta": {
        "note": (
            "Rust HE reverses the collapse tuple order for a product of two "
            "superpose forms. CeTTa follows the official interpret_tuple "
            "cartesian recombination order."
        ),
        "spec_refs": [
            "he_metta_official_specs.md:211-233 Interpret tuple source-order/cartesian recombination"
        ],
        "rule_ids": ["minimal.collapse-superpose", "nondet.visible-bag"],
    },
    "tests/test_collapse_bind.metta": {
        "note": (
            "Rust HE renders collapse-bind bindings in its internal braces shape; "
            "CeTTa follows the documented (<atom> <Bindings>) tuple form."
        ),
        "spec_refs": [
            "he_metta_official_specs.md:89 collapse-bind interprets the atom and returns a tuple containing all results"
        ],
        "rule_ids": ["minimal.collapse-superpose", "nondet.visible-bag"],
    },
    "tests/test_collapse_bind_ground_atom.metta": {
        "note": (
            "Rust HE renders collapse-bind bindings in its internal braces shape; "
            "CeTTa follows the documented (<atom> <Bindings>) tuple form."
        ),
        "spec_refs": [
            "he_metta_official_specs.md:89 collapse-bind interprets the atom and returns a tuple containing all results"
        ],
        "rule_ids": ["minimal.collapse-superpose", "nondet.visible-bag"],
    },
    "tests/test_collapse_add_next_atom_from_collapse_bind_result.metta": {
        "note": (
            "This helper consumes CeTTa's documented collapse-bind pair shape; "
            "Rust HE exposes a different binding representation."
        ),
        "spec_refs": [
            "he_metta_official_specs.md:89 collapse-bind interprets the atom and returns a tuple containing all results"
        ],
        "rule_ids": ["minimal.collapse-superpose", "nondet.visible-bag"],
    },
    "tests/test_get_type.metta": {
        "note": (
            "CeTTa keeps the local tuple-cap typing assertion as the intended "
            "type-system behavior; Rust HE reports extra tuple-cap types on the "
            "last assertion."
        ),
        "spec_refs": [
            "he_metta_official_specs.md:124 metta dispatches Symbol/Grounded/() atoms through type_cast",
            "he_metta_official_specs.md:146-156 type_cast returns successful type matches or BadType errors",
        ],
        "rule_ids": ["special.type-assignment", "special.function-type"],
    },
    "tests/test_quote_substitution_no_reapply_regression.metta": {
        "note": (
            "The official HE snapshot does not pin quote hygiene. CeTTa keeps the "
            "hygienic no-reapply behavior; Rust HE re-applies and fails the "
            "regression."
        ),
        "spec_refs": [
            "he_metta_official_specs.md:36 special-form recognizers do not cover every expression using a special atom",
            "he_metta_official_specs.md:89 minimal instruction list has no quote hygiene/reapplication rule",
        ],
        "rule_ids": ["minimal.eval", "special.eq-definition"],
    },
    "tests/test_superpose_eval_side_effects.metta": {
        "note": (
            "The official HE snapshot does not pin side-effect ordering under "
            "collapse. CeTTa keeps its regression behavior; Rust HE differs in "
            "the observed order."
        ),
        "spec_refs": [
            "he_metta_official_specs.md:89 collapse-bind collects all interpretation results",
            "he_metta_official_specs.md:211-233 interpret_tuple specifies source-order head/tail recombination but not side-effect scheduling",
        ],
        "rule_ids": ["minimal.collapse-superpose", "nondet.visible-bag"],
    },
    "tests/test_cverify_once_collapse_probe.metta": {
        "note": (
            "Rust HE stops collapse(eval(coin)) at the singleton one-step eval "
            "packet ((superpose ...)). CeTTa follows collapse-bind/interpret_tuple "
            "frontier collection and exposes the visible superpose results as the "
            "tuple (heads tails)."
        ),
        "spec_refs": [
            "he_metta_official_specs.md:89 eval makes one evaluation step; collapse-bind collects all interpretation results",
            "he_metta_official_specs.md:211-233 interpret_tuple source-order head/tail recombination",
            "he_metta_official_specs.md:351-389 metta_call re-enters evaluator/query results and returns Empty only for zero results",
        ],
        "rule_ids": ["minimal.eval", "minimal.collapse-superpose", "nondet.visible-bag"],
    },
    "tests/test_stdlib_helper_tranche.metta": {
        "note": (
            "Rust HE leaks an internal fresh result variable when the shared "
            "return-on-error helper handles Empty inside function/return. CeTTa "
            "keeps Empty as the documented no-result/special-result value."
        ),
        "spec_refs": [
            "he_metta_official_specs.md:73-75 Empty is a special no-result value distinct from unit",
            "he_metta_official_specs.md:89 function evaluates until return and return finishes the outer function",
            "he_metta_official_specs.md:117-118 metta preserves Empty/Error as special results",
        ],
        "rule_ids": ["result.empty-zero", "minimal.function-return", "nondet.visible-bag"],
    },
    "tests/test_foldl_atom_chain_collapse.metta": {
        "note": (
            "CeTTa keeps chain-bound and let-bound collapse packets stable for "
            "foldl-atom. Rust HE diverges on the chain form even though the "
            "minimal instructions define chain as substitution of the "
            "interpreted result into the template and collapse-bind/collapse as "
            "collecting all results."
        ),
        "spec_refs": [
            "he_metta_official_specs.md:89 chain substitutes each interpreted result into the template",
            "he_metta_official_specs.md:89 collapse-bind collects all interpretation results",
            "he_metta_official_specs.md:211-233 interpret_tuple source-order recombination",
        ],
        "rule_ids": ["minimal.chain", "minimal.collapse-superpose", "nondet.visible-bag"],
    },
    "tests/spec_profile_fold_by_key_extension.metta": {
        "note": (
            "The head is inert in Rust-compat probing, so this reduces to "
            "applicative argument traversal under an unknown head. CeTTa keeps "
            "the official interpret_tuple source order; Rust HE reverses the "
            "superpose-derived argument order."
        ),
        "spec_refs": [
            "he_metta_official_specs.md:205-210 non-function/%Undefined% heads use interpret_tuple and metta_call",
            "he_metta_official_specs.md:211-233 interpret_tuple source-order head/tail recombination",
        ],
        "rule_ids": ["minimal.collapse-superpose", "nondet.visible-bag"],
    },
    "tests/test_fold_by_key.metta": {
        "note": (
            "The visible broad-corpus mismatch is inert-head applicative "
            "argument order for fold-by-key calls in Rust-compat probing. "
            "CeTTa follows interpret_tuple source order; Rust HE reverses the "
            "superpose-derived tuple order."
        ),
        "spec_refs": [
            "he_metta_official_specs.md:205-210 non-function/%Undefined% heads use interpret_tuple and metta_call",
            "he_metta_official_specs.md:211-233 interpret_tuple source-order head/tail recombination",
        ],
        "rule_ids": ["minimal.collapse-superpose", "nondet.visible-bag"],
    },
}

BROAD_MODULE_RESOLUTION_NOTES: dict[str, dict[str, Any]] = {
    "tests/test_import_cycle.metta": {
        "note": (
            "CeTTa resolves the local-path import and exercises cycle detection; "
            "Rust HE rejects the local-path module name before cycle semantics "
            "are reached."
        ),
        "spec_refs": [
            "he_metta_official_specs.md:94 module hierarchy is acknowledged, but local-path import syntax and cycle semantics are not pinned"
        ],
        "rule_ids": ["surface.module-import"],
    },
    "tests/test_import_fresh_name_conformance.metta": {
        "note": (
            "CeTTa resolves a local support module into an explicitly named "
            "space and checks that the top space is unchanged; Rust HE rejects "
            "the local-path module name before fresh-name import behavior is "
            "reached."
        ),
        "spec_refs": [
            "he_metta_official_specs.md:94 atom-by-atom effects are specified, but local-path import spaces are not pinned"
        ],
        "rule_ids": ["surface.module-import", "surface.atomspace-assertions"],
    },
    "tests/test_import_nested_depth.metta": {
        "note": (
            "CeTTa resolves nested local-path imports; Rust HE rejects the "
            "local root path before nested-depth semantics are reached."
        ),
        "spec_refs": [
            "he_metta_official_specs.md:94 module hierarchy is acknowledged, but nested local-path import resolution is not pinned"
        ],
        "rule_ids": ["surface.module-import"],
    },
    "tests/test_import_parse_failure.metta": {
        "note": (
            "CeTTa resolves the local path and reports the module parse failure; "
            "Rust HE rejects the local-path module name before parse-failure "
            "propagation is reached."
        ),
        "spec_refs": [
            "he_metta_official_specs.md:80-82 Error atoms are specified, but local-path import parse-failure shape is not pinned",
            "he_metta_official_specs.md:94 module hierarchy is acknowledged, but local-path import syntax is not pinned",
        ],
        "rule_ids": ["surface.module-import", "result.error-shape"],
    },
    "tests/test_import_registered_nested_parent.metta": {
        "note": (
            "CeTTa resolves a registered parent module and its nested child; "
            "Rust HE accepts the registered parent but rejects the nested path "
            "before nested registered-import behavior is reached."
        ),
        "spec_refs": [
            "he_metta_official_specs.md:94 module hierarchy is acknowledged, but registered nested import resolution is not pinned"
        ],
        "rule_ids": ["surface.module-import"],
    },
    "tests/test_import_transaction.metta": {
        "note": (
            "CeTTa resolves the local path and exercises import rollback after "
            "a body error; Rust HE rejects the local-path module name before "
            "transactional import behavior is reached."
        ),
        "spec_refs": [
            "he_metta_official_specs.md:80-82 Error atoms are specified, but import transaction failure shape is not pinned",
            "he_metta_official_specs.md:94 atom-by-atom effects are specified, but local-path import transaction semantics are not pinned",
        ],
        "rule_ids": ["surface.module-import", "result.error-shape"],
    },
}

BROAD_MODULE_INVENTORY_NOTES: dict[str, dict[str, Any]] = {
    "tests/spec_print_mods_inventory.metta": {
        "note": (
            "CeTTa prints the explicitly loaded module graph for the current "
            "session, while Rust HE includes host/builtin modules in its "
            "inventory. The official HE snapshot acknowledges module hierarchy "
            "but does not pin print-mods! inventory contents or builtin module "
            "visibility."
        ),
        "spec_refs": [
            "he_metta_official_specs.md:94 module hierarchy is acknowledged, but module inventory/print-mods contents are not pinned"
        ],
        "rule_ids": [
            "surface.module-import",
            "surface.module-inventory",
            "profile.surface-policy",
        ],
    },
}

BROAD_LOCAL_LIBRARY_IMPORT_PATHS = {
    "tests/test_bio_wmpln_checkpoint_regression.metta",
    "tests/test_bio_wmpln_supported_key_unique_regression.metta",
    "tests/test_checkpoint_group_extract_cross_form_regression.metta",
    "tests/test_closed_stream_fastpath.metta",
    "tests/test_he_frontier_algebra_regression.metta",
    "tests/test_list_surface.metta",
    "tests/test_lts_he_surface.metta",
    "tests/test_lts_surface.metta",
    "tests/test_math_lib_surface.metta",
    "tests/test_math_namespace_docs.metta",
    "tests/test_nested_pipeline_tailrec.metta",
    "tests/test_string_escapes.metta",
}

BROAD_LOCAL_LIBRARY_IMPORT_MODULES = {
    "clist",
    "cverify_compat",
    "cverify_textual_parse",
    "fs",
    "list",
    "lts",
    "math",
    "metamath",
    "path",
    "pverify-utils",
    "rho",
    "str",
    "system",
    "text_cursor",
    "text_stream",
}

OSLF_ANCHORS = {
    "language_def": (
        "lean-projects/mettapedia/Mettapedia/Languages/MeTTa/HE/"
        "HELanguageDef.lean:mettaHE"
    ),
    "language_def_simulation": (
        "lean-projects/mettapedia/Mettapedia/Languages/MeTTa/HE/"
        "LanguageDefSimulation.lean:evalSpec_families_covered"
    ),
    "executable_boundary": (
        "lean-projects/mettapedia/Mettapedia/Languages/MeTTa/HE/"
        "ExecutableBoundary.lean:EvalAtomCertified"
    ),
    "correctness": (
        "lean-projects/mettapedia/Mettapedia/Languages/MeTTa/HE/"
        "Correctness.lean:allSound"
    ),
    "declarative_spec": (
        "lean-projects/mettapedia/Mettapedia/Languages/MeTTa/HE/"
        "DeclarativeSpec.lean:emptyOrErrorPassthrough_intro"
    ),
    "type_synthesis": (
        "lean-projects/mettapedia/Mettapedia/OSLF/Framework/"
        "TypeSynthesis.lean:langDiamondUsing/langBoxUsing/langGaloisUsing"
    ),
    "type_synthesis_modal_spec": (
        "lean-projects/mettapedia/Mettapedia/OSLF/Framework/"
        "TypeSynthesis.lean:langReduces/langDiamondUsing_spec/langBoxUsing_spec"
    ),
    "formula_modal_semantics": (
        "lean-projects/mettapedia/Mettapedia/OSLF/"
        "Formula.lean:sem_dia_eq_langDiamondUsing/sem_box_eq_langBoxUsing"
    ),
    "premise_reduction": (
        "lean-projects/mettapedia/Mettapedia/OSLF/MeTTaIL/"
        "DeclReducesWithPremises.lean"
    ),
}

TRACK_A_RULE_LANES = {
    "surface.eval-bang": "supporting-surface",
    "surface.atomspace-assertions": "supporting-surface",
    "special.eq-definition": "reduction-span",
    "special.type-assignment": "reduction-span",
    "special.function-type": "reduction-span",
    "result.empty-zero": "reduction-span",
    "result.error-shape": "reduction-span",
    "minimal.eval": "reduction-span",
    "minimal.evalc-context-space": "supporting-surface",
    "minimal.chain": "reduction-span",
    "minimal.match": "supporting-surface",
    "minimal.unify": "supporting-surface",
    "minimal.collapse-superpose": "reduction-span",
    "minimal.cons-decons": "supporting-surface",
    "minimal.function-return": "reduction-span",
    "eval.applicative-arguments": "supporting-surface",
    "eval.grounded-dispatch": "supporting-surface",
    "nondet.visible-bag": "reduction-span",
    "profile.docs": "supporting-surface",
    "profile.surface-policy": "spatial-policy",
    "surface.module-import": "spatial-policy",
    "surface.module-inventory": "spatial-policy",
    "surface.native-space": "spatial-policy",
    "grounded.numeric-exact": "supporting-surface",
}

TRACK_A_LANE_NOTES = {
    "reduction-span": (
        "This capacity is treated as part of the current Track A reduction span: "
        "it is intended to line up with LanguageDef/langReduces/langDiamond-style "
        "reasoning rather than with runtime policy or host environment."
    ),
    "supporting-surface": (
        "This capacity supports the evaluator/runtime story but is not currently "
        "promoted as one of the core reduction-span edges in the Track A A2 slice."
    ),
    "spatial-policy": (
        "This capacity stays in the spatial/policy/meta lane for now. It is visible "
        "in the catalog on purpose, but it is not part of the current reduction-span "
        "slice consumed by Track A modal reasoning."
    ),
}

A2_LEAN_AUTHORITY_REFS = {
    "minimal.eval": [
        "MinimalMeTTa.MinimalStep.eval",
        "HELanguageDef.mettaHE:MC_Grounded",
        "HELanguageDef.mettaHE:MC_Equation",
        "HELanguageDef.mettaHE:MC_NoMatch",
        "LanguageDefSimulation.evalSpec_families_covered",
        "Conformance.mettaCall_no_match",
        "Correctness.eqMatch_sound",
        "ExecutableBoundary.EvalAtomCertified",
    ],
    "result.empty-zero": [
        "EvalSpec.EvalAtom.empty_or_error",
        "HELanguageDef.mettaHE:M_Empty",
        "HELanguageDef.mettaHE:MC_Superpose_Empty",
        "HELanguageDef.mettaHE:MC_Match_Empty",
        "LanguageDefSimulation.evalSpec_families_covered",
        "DeclarativeSpec.emptyOrErrorPassthrough_intro",
        "Conformance.eval_empty_passthrough",
        "ExecutableBoundary.EvalAtomCertified",
    ],
}

A2_AUTHORITY_NOTES = {
    "minimal.eval": (
        "Current A2 authority is layered: the minimal instruction exists as "
        "MinimalMeTTa.MinimalStep.eval; the executable HE LanguageDef realizes "
        "the same observable slice through the MC_Grounded / MC_Equation / MC_NoMatch "
        "rewrite family; LanguageDefSimulation and Correctness provide the current "
        "Lean-side bridge into EvalSpec/executable support. Direct CeTTa↔Lean adequacy "
        "remains a tested catalog claim rather than a proved extraction theorem."
    ),
    "result.empty-zero": (
        "Current A2 authority is likewise layered: EvalSpec's empty_or_error "
        "constructor and the M_Empty / MC_Superpose_Empty / MC_Match_Empty "
        "LanguageDef rules pin the no-visible-result behavior, while the catalog "
        "and runtime contracts keep the visible bag/LTS consequences honest. "
        "This is a reduction-span rule in Track A, not a host-policy surface."
    ),
}

SPEC_RULE_CAPACITIES: list[dict[str, Any]] = [
    {
        "id": "surface.eval-bang",
        "title": "Top-level ! evaluation",
        "official_spec_refs": [
            "he_metta_official_specs.md:Interpretation - atoms prefixed by ! are evaluated",
        ],
        "lean_language_def_refs": ["HELanguageDef.mettaHE:Metta", "M_Expression"],
        "oslf_refs": [OSLF_ANCHORS["language_def"], OSLF_ANCHORS["type_synthesis"]],
        "native_contract_ids": ["profile-he-formal-vs-rust-compat-split"],
        "status": "covered",
    },
    {
        "id": "surface.atomspace-assertions",
        "title": "Atomspace declarations and assertion-style programs",
        "official_spec_refs": [
            "he_metta_official_specs.md:Interpretation - non-! atoms are added to the atomspace",
        ],
        "lean_language_def_refs": ["HELanguageDef.mettaHE:Space", "MC_Equation"],
        "oslf_refs": [OSLF_ANCHORS["language_def"]],
        "native_contract_ids": ["profile-surface-policy-table"],
        "status": "partial",
    },
    {
        "id": "special.eq-definition",
        "title": "Equation definitions",
        "official_spec_refs": ["he_metta_official_specs.md:Special expressions - ="],
        "lean_language_def_refs": ["HELanguageDef.mettaHE:EqAtom", "MC_Equation", "MC_NoMatch"],
        "oslf_refs": [OSLF_ANCHORS["language_def"], OSLF_ANCHORS["premise_reduction"]],
        "native_contract_ids": ["equation-query-successor-shape"],
        "status": "covered",
    },
    {
        "id": "special.type-assignment",
        "title": "Type assignment and type lookup",
        "official_spec_refs": ["he_metta_official_specs.md:Special expressions - :"],
        "lean_language_def_refs": [
            "HELanguageDef.mettaHE:TypeAnnotation",
            "IE_FuncType",
            "IE_TupleType",
            "TC_Match",
            "TC_Mismatch",
        ],
        "oslf_refs": [OSLF_ANCHORS["language_def"], OSLF_ANCHORS["premise_reduction"]],
        "native_contract_ids": [
            "function-type-applicability-premise-flow",
            "type-cast-successor-shape",
        ],
        "status": "partial",
    },
    {
        "id": "special.function-type",
        "title": "Function type arrows",
        "official_spec_refs": ["he_metta_official_specs.md:Special expressions - ->"],
        "lean_language_def_refs": ["HELanguageDef.mettaHE:ArrowType", "IE_FuncType"],
        "oslf_refs": [OSLF_ANCHORS["language_def"]],
        "native_contract_ids": ["function-type-applicability-premise-flow"],
        "status": "partial",
    },
    {
        "id": "result.empty-zero",
        "title": "Empty as no visible result",
        "official_spec_refs": [
            "he_metta_official_specs.md:Special function results - Empty",
            "he_metta_official_specs.md:Minimal MeTTa result collection",
        ],
        "lean_language_def_refs": [
            "HELanguageDef.mettaHE:M_Empty",
            "M_Empty",
            "MC_Superpose_Empty",
            "MC_Match_Empty",
        ],
        "oslf_refs": [
            OSLF_ANCHORS["type_synthesis"],
            OSLF_ANCHORS["type_synthesis_modal_spec"],
            OSLF_ANCHORS["formula_modal_semantics"],
        ],
        "native_contract_ids": [
            "empty-zero-visible-result-bag",
            "empty-not-lts-successor",
        ],
        "status": "covered",
    },
    {
        "id": "result.error-shape",
        "title": "Error propagation and error constructors",
        "official_spec_refs": ["he_metta_official_specs.md:Special function results - Error"],
        "lean_language_def_refs": [
            "HELanguageDef.mettaHE:ErrorAtom",
            "M_Error",
            "MC_Error",
            "TC_Mismatch",
        ],
        "oslf_refs": [OSLF_ANCHORS["language_def"]],
        "native_contract_ids": ["error-successor-shape"],
        "status": "covered",
    },
    {
        "id": "minimal.eval",
        "title": "Minimal MeTTa eval instruction",
        "official_spec_refs": ["he_metta_official_specs.md:Minimal MeTTa instructions - eval"],
        "lean_language_def_refs": [
            "MinimalMeTTa.MinimalStep.eval",
            "HELanguageDef.mettaHE:MC_Grounded",
            "HELanguageDef.mettaHE:MC_Equation",
            "HELanguageDef.mettaHE:MC_NoMatch",
        ],
        "oslf_refs": [
            OSLF_ANCHORS["language_def"],
            OSLF_ANCHORS["type_synthesis"],
            OSLF_ANCHORS["type_synthesis_modal_spec"],
        ],
        "native_contract_ids": ["eval-successor-shape"],
        "status": "covered",
    },
    {
        "id": "minimal.evalc-context-space",
        "title": "evalc and context-space",
        "official_spec_refs": [
            "he_metta_official_specs.md:Minimal MeTTa instructions - evalc/context-space",
        ],
        "lean_language_def_refs": ["HELanguageDef.mettaHE:State", "HELanguageDef.mettaHE:Space"],
        "oslf_refs": [OSLF_ANCHORS["language_def"]],
        "native_contract_ids": ["context-space-successor-shape"],
        "status": "partial",
    },
    {
        "id": "minimal.chain",
        "title": "chain premise flow",
        "official_spec_refs": ["he_metta_official_specs.md:Minimal MeTTa instructions - chain"],
        "lean_language_def_refs": ["HELanguageDef.mettaHE:MettaCall", "R_Done"],
        "oslf_refs": [OSLF_ANCHORS["premise_reduction"], OSLF_ANCHORS["type_synthesis"]],
        "native_contract_ids": ["chain-premise-flow", "chain-successor-shape"],
        "status": "covered",
    },
    {
        "id": "minimal.match",
        "title": "match against the active space",
        "official_spec_refs": ["he_metta_official_specs.md:Minimal MeTTa instructions - match"],
        "lean_language_def_refs": ["MC_Match", "MC_Match_Empty"],
        "oslf_refs": [OSLF_ANCHORS["premise_reduction"], OSLF_ANCHORS["type_synthesis"]],
        "native_contract_ids": ["match-premise-flow", "match-successor-shape"],
        "status": "covered",
    },
    {
        "id": "minimal.unify",
        "title": "local unify success/failure branches",
        "official_spec_refs": ["he_metta_official_specs.md:Minimal MeTTa instructions - unify"],
        "lean_language_def_refs": ["MC_Unify_Match", "MC_Unify_NoMatch"],
        "oslf_refs": [OSLF_ANCHORS["premise_reduction"], OSLF_ANCHORS["type_synthesis"]],
        "native_contract_ids": ["unify-premise-flow", "unify-successor-shape"],
        "status": "covered",
    },
    {
        "id": "minimal.collapse-superpose",
        "title": "collapse/superpose nondeterministic bags",
        "official_spec_refs": [
            "he_metta_official_specs.md:Minimal MeTTa instructions - collapse-bind/superpose-bind",
        ],
        "lean_language_def_refs": [
            "MC_Collapse",
            "MC_Superpose",
            "MC_Superpose_Empty",
        ],
        "oslf_refs": [OSLF_ANCHORS["type_synthesis"]],
        "native_contract_ids": [
            "superpose-successor-shape",
            "collapse-successor-shape",
            "empty-zero-visible-result-bag",
        ],
        "status": "covered",
    },
    {
        "id": "minimal.cons-decons",
        "title": "cons/decons atom structure",
        "official_spec_refs": [
            "he_metta_official_specs.md:Minimal MeTTa instructions - cons-atom/decons-atom",
        ],
        "lean_language_def_refs": ["HELanguageDef.mettaHE:ExprCons", "HELanguageDef.mettaHE:ExprNil"],
        "oslf_refs": [OSLF_ANCHORS["language_def"]],
        "native_contract_ids": ["constructor-arity-surface"],
        "status": "covered",
    },
    {
        "id": "minimal.function-return",
        "title": "function/return control flow",
        "official_spec_refs": [
            "he_metta_official_specs.md:Minimal MeTTa instructions - function/return",
        ],
        "lean_language_def_refs": ["HELanguageDef.mettaHE:Return", "R_Done"],
        "oslf_refs": [OSLF_ANCHORS["language_def"]],
        "native_contract_ids": ["function-return-successor-shape"],
        "status": "covered",
    },
    {
        "id": "eval.applicative-arguments",
        "title": "Applicative argument evaluation",
        "official_spec_refs": [
            "he_metta_official_specs.md:Interpret expression / interpret function / interpret args",
        ],
        "lean_language_def_refs": [
            "IE_FuncType",
            "IF_Start",
            "IF_AfterOp_EvalArgs",
            "IA_Start_Typed",
            "IA_Start_Undef",
        ],
        "oslf_refs": [OSLF_ANCHORS["language_def"], OSLF_ANCHORS["premise_reduction"]],
        "native_contract_ids": [
            "argument-evaluation-premise-flow",
            "surviving-branch-reenters-evaluator",
        ],
        "status": "covered",
    },
    {
        "id": "eval.grounded-dispatch",
        "title": "Grounded dispatch and native results",
        "official_spec_refs": [
            "he_metta_official_specs.md:Interpret function / mettaCall grounded dispatch",
        ],
        "lean_language_def_refs": ["MC_Grounded"],
        "oslf_refs": [OSLF_ANCHORS["premise_reduction"]],
        "native_contract_ids": ["grounded-dispatch-successor-shape"],
        "status": "covered",
    },
    {
        "id": "nondet.visible-bag",
        "title": "Nondeterministic visible result bags",
        "official_spec_refs": ["he_metta_official_specs.md:Minimal MeTTa result collection"],
        "lean_language_def_refs": ["MC_Superpose", "MC_Match", "R_Done"],
        "oslf_refs": [OSLF_ANCHORS["type_synthesis"]],
        "native_contract_ids": [
            "empty-zero-visible-result-bag",
            "lts-diamond-frontier-shape",
        ],
        "status": "covered",
    },
    {
        "id": "profile.docs",
        "title": "Profile-aware documentation surface",
        "official_spec_refs": ["he_metta_official_specs.md:profile/lang surfaces"],
        "lean_language_def_refs": [],
        "oslf_refs": [],
        "native_contract_ids": [
            "get-doc-respects-profile-surface",
            "he-compat-reproduces-rust-get-doc-arity",
        ],
        "status": "covered",
    },
    {
        "id": "profile.surface-policy",
        "title": "Language/profile surface availability",
        "official_spec_refs": ["he_metta_official_specs.md:profile/lang surfaces"],
        "lean_language_def_refs": [],
        "alignment_note": (
            "Current HELanguageDef models core HE evaluation rules, not the "
            "CeTTa profile/lang surface inventory. This rule is tracked by "
            "runtime policy tables plus OSLF-facing profile evidence."
        ),
        "oslf_refs": [OSLF_ANCHORS["language_def"]],
        "native_contract_ids": ["profile-surface-policy-table"],
        "status": "partial",
    },
    {
        "id": "surface.module-import",
        "title": "Module import/include resolution",
        "official_spec_refs": ["he_metta_official_specs.md:module/import surfaces"],
        "lean_language_def_refs": [],
        "alignment_note": (
            "Module/import resolution is currently a runtime/meta-surface, not "
            "a first-class HELanguageDef rule family. The rule index keeps it "
            "visible as a formal-he policy surface and a he-compat boundary."
        ),
        "oslf_refs": [],
        "native_contract_ids": [
            "profile-surface-policy-table",
            "module-import-policy-evidence-shape",
        ],
        "status": "partial",
    },
    {
        "id": "surface.module-inventory",
        "title": "Module inventory and visible module tree",
        "official_spec_refs": ["he_metta_official_specs.md:profile/lang module surfaces"],
        "lean_language_def_refs": [],
        "alignment_note": (
            "Visible module inventory is not represented in the current "
            "HELanguageDef rewrite relation. It remains a policy/documentation "
            "surface until we model module visibility at the LanguageDef/space "
            "layer."
        ),
        "oslf_refs": [],
        "native_contract_ids": [
            "profile-surface-policy-table",
            "module-inventory-policy-evidence-shape",
        ],
        "status": "open",
    },
    {
        "id": "surface.native-space",
        "title": "Native space operations and space-kind extensions",
        "official_spec_refs": ["he_metta_official_specs.md:space operation surfaces"],
        "lean_language_def_refs": ["HELanguageDef.mettaHE:Space", "HELanguageDef.mettaHE:State"],
        "oslf_refs": [OSLF_ANCHORS["language_def"], OSLF_ANCHORS["type_synthesis"]],
        "native_contract_ids": ["context-space-successor-shape"],
        "status": "partial",
    },
    {
        "id": "grounded.numeric-exact",
        "title": "Grounded numeric and exact-rational extensions",
        "official_spec_refs": ["he_metta_official_specs.md:grounded arithmetic surfaces"],
        "lean_language_def_refs": ["HELanguageDef.mettaHE:MC_Grounded"],
        "oslf_refs": [OSLF_ANCHORS["premise_reduction"]],
        "native_contract_ids": ["grounded-dispatch-successor-shape"],
        "status": "partial",
    },
]

CASE_RULE_OVERRIDES: dict[str, list[str]] = {
    "tests/io_he_ext_tv_conjunction_reduces.metta": [
        "special.eq-definition",
        "minimal.match",
        "eval.applicative-arguments",
        "grounded.numeric-exact",
    ],
    "tests/io_he_ext_tv_implication_reduces.metta": [
        "special.eq-definition",
        "minimal.match",
        "eval.applicative-arguments",
        "grounded.numeric-exact",
    ],
    "tests/io_he_ext_type_prop_application_type.metta": [
        "special.type-assignment",
        "special.function-type",
    ],
    "tests/io_he_ext_type_prop_bad_proof_rejected.metta": [
        "special.type-assignment",
        "special.function-type",
        "result.empty-zero",
    ],
    "tests/he_c3_pln_stv.metta": [
        "special.eq-definition",
        "minimal.match",
        "minimal.collapse-superpose",
        "eval.applicative-arguments",
        "nondet.visible-bag",
        "result.empty-zero",
    ],
    "tests/he_d4_type_prop.metta": [
        "special.eq-definition",
        "special.type-assignment",
        "minimal.match",
        "nondet.visible-bag",
        "result.empty-zero",
    ],
    "tests/he_g1_docs.metta": ["profile.docs", "result.error-shape"],
    "tests/test_eval_grounded.metta": ["minimal.eval", "eval.grounded-dispatch"],
    "tests/test_no_return_error.metta": ["minimal.function-return", "result.error-shape"],
    "tests/test_he_frontier_algebra_regression.metta": [
        "result.empty-zero",
        "nondet.visible-bag",
        "minimal.match",
        "minimal.collapse-superpose",
    ],
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


def case_source_excerpt(case_path: Path, max_chars: int = 8192) -> str:
    if not case_path.exists():
        return ""
    return case_path.read_text(encoding="utf-8", errors="ignore")[:max_chars].lower()


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


def capacity_by_id() -> dict[str, dict[str, Any]]:
    return {str(row["id"]): row for row in spec_rule_capacities()}


def track_a_rule_lane(rule_id: str) -> str:
    return TRACK_A_RULE_LANES.get(rule_id, "supporting-surface")


def validate_track_a_rule_lanes() -> None:
    known_ids = {str(row["id"]) for row in SPEC_RULE_CAPACITIES}
    missing = sorted(rule_id for rule_id in known_ids if rule_id not in TRACK_A_RULE_LANES)
    extra = sorted(rule_id for rule_id in TRACK_A_RULE_LANES if rule_id not in known_ids)
    errors: list[str] = []
    if missing:
        errors.append("missing lane assignments: " + ", ".join(missing))
    if extra:
        errors.append("lane assignments for unknown capacities: " + ", ".join(extra))
    if errors:
        raise SystemExit("invalid Track A rule-lane table:\n  - " + "\n  - ".join(errors))


def spec_rule_capacities() -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for base in SPEC_RULE_CAPACITIES:
        row = dict(base)
        rule_id = str(row["id"])
        row["track_a_relation_lane"] = track_a_rule_lane(rule_id)
        row["track_a_lane_note"] = TRACK_A_LANE_NOTES[row["track_a_relation_lane"]]
        if rule_id in A2_LEAN_AUTHORITY_REFS:
            row["lean_authority_refs"] = list(A2_LEAN_AUTHORITY_REFS[rule_id])
            row["authority_note"] = A2_AUTHORITY_NOTES[rule_id]
        rows.append(row)
    return rows


def validate_broad_note_table(
    table: dict[str, dict[str, Any]],
    label: str,
) -> None:
    known_rules = capacity_by_id()
    errors: list[str] = []
    for path, note in sorted(table.items()):
        if not str(note.get("note", "")).strip():
            errors.append(f"{path}: missing {label} note")
        spec_refs = note.get("spec_refs")
        if not isinstance(spec_refs, list) or not all(str(ref).strip() for ref in spec_refs):
            errors.append(f"{path}: missing {label} spec refs")
        rule_ids = note.get("rule_ids")
        if not isinstance(rule_ids, list) or not all(str(rule_id).strip() for rule_id in rule_ids):
            errors.append(f"{path}: missing {label} rule ids")
            continue
        unknown = sorted(str(rule_id) for rule_id in rule_ids if str(rule_id) not in known_rules)
        if unknown:
            errors.append(f"{path}: unknown {label} rule ids: {', '.join(unknown)}")
    if errors:
        details = "\n  - ".join(errors)
        raise SystemExit(f"invalid broad {label} notes:\n  - {details}")


def validate_broad_rust_drift_notes() -> None:
    validate_broad_note_table(BROAD_RUST_DRIFT_NOTES, "rust-drift")


def validate_broad_module_resolution_notes() -> None:
    validate_broad_note_table(BROAD_MODULE_RESOLUTION_NOTES, "module-resolution")


def validate_broad_module_inventory_notes() -> None:
    validate_broad_note_table(BROAD_MODULE_INVENTORY_NOTES, "module-inventory")


def infer_spec_rule_ids(row: dict[str, Any]) -> list[str]:
    path = str(row.get("cetta_path", ""))
    case_id = str(row.get("id", ""))
    feature = str(row.get("feature", ""))
    source_kind = str(row.get("source_kind", ""))
    refs = " ".join(str(ref).lower() for ref in row.get("spec_refs", []))
    source_excerpt = case_source_excerpt(ROOT / path) if path else ""

    ids = {"surface.eval-bang", "nondet.visible-bag"}
    ids.update(CASE_RULE_OVERRIDES.get(path, []))

    text = " ".join([path, case_id, feature, source_kind, refs]).lower()
    if "assert" in text:
        ids.add("surface.atomspace-assertions")
        ids.add("result.error-shape")
    if "case" in text or "switch" in text:
        ids.add("eval.applicative-arguments")
    if "chain" in text or "backchain" in text:
        ids.add("minimal.chain")
    if "collapse" in text or "superpose" in text or "nondet" in text or "choose" in text:
        ids.add("minimal.collapse-superpose")
    if "match" in text or "pattern" in text or "substitution" in text or "twoside" in text:
        ids.add("minimal.match")
    if "unify" in text:
        ids.add("minimal.unify")
    if "cons" in text or "decons" in text:
        ids.add("minimal.cons-decons")
    if "eval" in text or "rewrite" in text or "unknown_expr_preserved" in text:
        ids.add("minimal.eval")
        ids.add("special.eq-definition")
    if "grounded" in text or "float" in text or "string" in text:
        ids.add("eval.grounded-dispatch")
    if "type" in text or "gadt" in text or "deptypes" in text:
        ids.add("special.type-assignment")
        ids.add("special.function-type")
    if "function" in text or "return" in text or "no_return" in text:
        ids.add("minimal.function-return")
    if "empty" in text:
        ids.add("result.empty-zero")
    if "error" in text or "bad_type" in text:
        ids.add("result.error-shape")
    if "context_space" in text or "evalc" in text or "space" in text:
        ids.add("minimal.evalc-context-space")
    if "doc" in text or "help" in text:
        ids.add("profile.docs")
    if "profile" in text or "surface" in text or "compat" in text or "extended" in text:
        ids.add("profile.surface-policy")
    if "import" in text or "include" in text or "module" in text:
        ids.add("surface.module-import")
    if (
        "module-inventory" in text
        or "module_inventory" in text
        or "print-mods" in text
        or "print_mods" in text
        or "print-mods" in source_excerpt
        or "print_mods" in source_excerpt
    ):
        ids.add("surface.module-inventory")
    if any(
        token in text or token in source_excerpt
        for token in [
            "new-space",
            "space-len",
            "space-get",
            "space-pop",
            "space-peek",
            "space-set-backend",
            "space-set-match-backend",
            "space_engine_backend",
            "hash_space",
            "queue_space",
            "stack_space",
        ]
    ):
        ids.add("surface.native-space")
    if "rational" in text or "bigint" in text or "numeric" in text or "min" in text or "max" in text:
        ids.add("grounded.numeric-exact")
    if "equation" in text or "rewrite" in text or "symbols" in text or "direct" in text:
        ids.add("special.eq-definition")
    if "opencoggy" in text or "pln" in text or "stv" in text:
        ids.add("eval.applicative-arguments")

    known = capacity_by_id()
    return sorted(rule_id for rule_id in ids if rule_id in known)


def enrich_case_with_rule_map(row: dict[str, Any]) -> dict[str, Any]:
    known = capacity_by_id()
    rule_ids = infer_spec_rule_ids(row)
    refs: set[str] = set()
    lean_refs: set[str] = set()
    lean_authority_refs: set[str] = set()
    oslf_refs: set[str] = set()
    native_contract_ids: set[str] = set()
    rule_lanes: set[str] = set()
    for rule_id in rule_ids:
        cap = known[rule_id]
        refs.update(str(x) for x in cap.get("official_spec_refs", []))
        lean_refs.update(str(x) for x in cap.get("lean_language_def_refs", []))
        lean_authority_refs.update(str(x) for x in cap.get("lean_authority_refs", []))
        oslf_refs.update(str(x) for x in cap.get("oslf_refs", []))
        native_contract_ids.update(str(x) for x in cap.get("native_contract_ids", []))
        rule_lanes.add(str(cap.get("track_a_relation_lane", "supporting-surface")))

    row["spec_rule_ids"] = rule_ids
    row["official_spec_rule_refs"] = sorted(refs)
    row["lean_language_def_refs"] = sorted(lean_refs)
    row["lean_authority_refs"] = sorted(lean_authority_refs)
    row["oslf_refs"] = sorted(oslf_refs)
    row["native_contract_ids"] = sorted(native_contract_ids)
    row["track_a_relation_lanes"] = sorted(rule_lanes)
    row["rule_mapping"] = {
        "status": "mapped" if rule_ids else "needs-rule-map",
        "method": (
            "case-path/feature heuristic over a dated rule-capacity table; "
            "tighten by replacing heuristic entries with Lean-exported "
            "LanguageDef rule evidence as the oracle link matures"
        ),
    }
    return row


def enrich_cases(cases: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [enrich_case_with_rule_map(row) for row in cases]


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

    return enrich_cases(sorted(cases, key=lambda row: (row["tier"], row["source_kind"], row["id"])))


def summarize(cases: list[dict[str, Any]]) -> dict[str, Any]:
    by_tier: dict[str, int] = {}
    by_classification: dict[str, int] = {}
    by_source_kind: dict[str, int] = {}
    by_spec_rule: dict[str, int] = {}
    by_track_a_relation_lane: dict[str, int] = {}
    unmapped_cases: list[str] = []
    for row in cases:
        by_tier[str(row["tier"])] = by_tier.get(str(row["tier"]), 0) + 1
        cls = str(row["classification"])
        by_classification[cls] = by_classification.get(cls, 0) + 1
        kind = str(row["source_kind"])
        by_source_kind[kind] = by_source_kind.get(kind, 0) + 1
        for lane in row.get("track_a_relation_lanes", []):
            lane_s = str(lane)
            by_track_a_relation_lane[lane_s] = by_track_a_relation_lane.get(lane_s, 0) + 1
        rule_ids = [str(rule_id) for rule_id in row.get("spec_rule_ids", [])]
        if not rule_ids:
            unmapped_cases.append(str(row["id"]))
        for rule_id in rule_ids:
            by_spec_rule[rule_id] = by_spec_rule.get(rule_id, 0) + 1
    return {
        "total": len(cases),
        "by_tier": by_tier,
        "by_classification": by_classification,
        "by_source_kind": by_source_kind,
        "by_spec_rule": dict(sorted(by_spec_rule.items())),
        "by_track_a_relation_lane": dict(sorted(by_track_a_relation_lane.items())),
        "mapped_case_count": len(cases) - len(unmapped_cases),
        "unmapped_cases": unmapped_cases,
        "covered_spec_rule_capacity_count": len(by_spec_rule),
        "total_spec_rule_capacity_count": len(spec_rule_capacities()),
        "tier0_1_needs_triage": sum(
            1
            for row in cases
            if int(row["tier"]) <= 1 and row["classification"] == "needs-triage"
        ),
    }


BROAD_CATEGORY_DETAILS: dict[str, dict[str, str]] = {
    "observed-agree": {
        "kind": "promotable",
        "recommended_action": "Promote as Tier 3 broad corpus evidence.",
    },
    "local-library-or-import-extension": {
        "kind": "mostly-formal-or-he-extended-extension",
        "recommended_action": (
            "Keep as formal-he/he-extended/local-library evidence unless Rust HE "
            "gains the module/import surface."
        ),
    },
    "module-resolution-diff": {
        "kind": "import-semantics-gap",
        "recommended_action": (
            "Keep out of core evaluator conformance unless a module/import "
            "contract is pinned. Normalize presentation-only names/error "
            "wording in the harness; do not contort CeTTa runtime errors to "
            "match host-specific Rust strings."
        ),
    },
    "module-inventory-environment-diff": {
        "kind": "environment-surface-gap",
        "recommended_action": "Do not treat as core semantics until module inventory policy is pinned.",
    },
    "native-space-extension": {
        "kind": "mostly-formal-or-he-extended-extension",
        "recommended_action": "Classify by space kind; only portable Rust-compatible space behavior enters he-compat.",
    },
    "grounded-numeric-extension": {
        "kind": "mostly-formal-or-he-extended-extension",
        "recommended_action": "Keep exact numeric behavior in formal/he-extended unless Rust-compatible behavior is required.",
    },
    "nondet-order-or-collapse-shape": {
        "kind": "core-semantics-needs-adjudication",
        "recommended_action": "Inspect manually; distinguish harmless order from real collapse/superpose semantics.",
    },
    "core-builtin-or-result-shape-gap": {
        "kind": "core-semantics-needs-adjudication",
        "recommended_action": "Inspect manually against the official spec and Rust HE output.",
    },
    "rust-drift": {
        "kind": "documented-upstream-divergence",
        "recommended_action": "Keep CeTTa behavior; record the spec/principle note instead of changing the evaluator toward Rust.",
    },
    "upstream-not-runnable": {
        "kind": "not-rust-runnable",
        "recommended_action": "Exclude from he-compat agreement counts; use as formal/he-extended pressure only.",
    },
    "upstream-timeout": {
        "kind": "not-rust-runnable",
        "recommended_action": "Exclude from he-compat agreement counts until timeout is isolated.",
    },
    "cetta-timeout": {
        "kind": "cetta-performance-or-termination-gap",
        "recommended_action": "Profile CeTTa on the case; if Rust returns promptly, treat as serious.",
    },
}

CLOSED_BROAD_CATEGORIES = {
    "core-builtin-or-result-shape-gap",
    "grounded-numeric-extension",
    "native-space-extension",
    "nondet-order-or-collapse-shape",
    "cetta-timeout",
}


def stable_id(prefix: str, path: str) -> str:
    stem = re.sub(r"[^A-Za-z0-9_.-]+", "-", path.removeprefix("tests/"))
    stem = stem.removesuffix(".metta").replace("/", "-")
    return f"{prefix}:{stem}"


def broad_output_text(row: dict[str, Any]) -> str:
    cetta = row.get("cetta", {})
    upstream = row.get("upstream", {})
    return " ".join(
        [
            str(row.get("path", "")),
            str(cetta.get("head", "")),
            str(upstream.get("head", "")),
        ]
    ).lower()


def upstream_module_resolution_failure(upstream_head: str) -> bool:
    return "Failed to resolve module top:" in upstream_head or "Illegal module name:" in upstream_head


def upstream_missing_module_name(upstream_head: str) -> str | None:
    match = re.search(r"Failed to resolve module top:([A-Za-z0-9_:-]+)", upstream_head)
    if match:
        return match.group(1).split(":", 1)[0]
    match = re.search(r"import! [^)]* ([A-Za-z0-9_:-]+)\)", upstream_head)
    if match:
        return match.group(1).split(":", 1)[0]
    return None


def broad_local_library_import(row: dict[str, Any], upstream_head: str) -> bool:
    path = str(row.get("path", ""))
    if path in BROAD_LOCAL_LIBRARY_IMPORT_PATHS:
        return True
    module = upstream_missing_module_name(upstream_head)
    if module in BROAD_LOCAL_LIBRARY_IMPORT_MODULES:
        return True
    return any(
        token in path
        for token in [
            "cverify",
            "pverify",
            "metamath",
            "rho",
            "pln",
            "bio",
            "text_",
            "clist",
            "fs_",
            "str_",
            "system_",
            "path_",
        ]
    )


def triage_broad_result(row: dict[str, Any]) -> str:
    classification = str(row.get("classification", ""))
    path = str(row.get("path", ""))
    text = broad_output_text(row)
    upstream_head = str(row.get("upstream", {}).get("head", ""))
    cetta_head = str(row.get("cetta", {}).get("head", ""))

    if classification == "agree":
        return "observed-agree"
    if path in BROAD_RUST_DRIFT_NOTES:
        return "rust-drift"
    if upstream_module_resolution_failure(upstream_head) and broad_local_library_import(row, upstream_head):
        return "local-library-or-import-extension"
    if classification in {"upstream-not-runnable", "upstream-timeout", "cetta-timeout"}:
        return classification
    if (
        "module-inventory" in path
        or "module_inventory" in path
        or "print_mods" in path
        or "print-mods" in path
    ):
        return "module-inventory-environment-diff"
    if "git-module!" in text or "git_module" in path:
        return "module-resolution-diff"
    if upstream_module_resolution_failure(upstream_head):
        return "module-resolution-diff"
    if "rational" in path or "bigint" in path or "numeric" in path or "1/2" in text or "999/1000" in text:
        return "grounded-numeric-extension"
    if any(token in text for token in ["new-space", "space-len", "space-get", "space-pop", "space-peek", "count-atoms"]):
        return "native-space-extension"
    if any(token in text for token in ["collapse", "superpose", "bindings", "width_tuple", "fold-by-key"]):
        return "nondet-order-or-collapse-shape"
    return "core-builtin-or-result-shape-gap"


def broad_rule_ids(row: dict[str, Any], category: str) -> list[str]:
    pseudo = {
        "id": stable_id("broad-he-manifest", str(row.get("path", ""))),
        "cetta_path": row.get("path", ""),
        "feature": f"{category} {row.get('manifest', {}).get('notes', '')}",
        "source_kind": "broad_manifest_probe",
        "spec_refs": [],
    }
    ids = set(infer_spec_rule_ids(pseudo))
    if category in {"local-library-or-import-extension", "module-resolution-diff"}:
        ids.add("surface.module-import")
        if category == "module-resolution-diff":
            module_note = BROAD_MODULE_RESOLUTION_NOTES.get(str(row.get("path", "")), {})
            ids.update(module_note.get("rule_ids", []))
    elif category == "module-inventory-environment-diff":
        ids.update(["surface.module-import", "surface.module-inventory", "profile.surface-policy"])
        inventory_note = BROAD_MODULE_INVENTORY_NOTES.get(str(row.get("path", "")), {})
        ids.update(inventory_note.get("rule_ids", []))
    elif category == "native-space-extension":
        ids.add("surface.native-space")
    elif category == "grounded-numeric-extension":
        ids.update(["grounded.numeric-exact", "eval.grounded-dispatch"])
    elif category == "nondet-order-or-collapse-shape":
        ids.update(["minimal.collapse-superpose", "nondet.visible-bag"])
    elif category == "rust-drift":
        drift = BROAD_RUST_DRIFT_NOTES.get(str(row.get("path", "")), {})
        ids.update(drift.get("rule_ids", []))
    elif category == "cetta-timeout":
        ids.update(["surface.eval-bang", "nondet.visible-bag"])
    known = capacity_by_id()
    return sorted(rule_id for rule_id in ids if rule_id in known)


def compact_probe_evidence(row: dict[str, Any]) -> dict[str, Any]:
    cetta = row.get("cetta", {})
    upstream = row.get("upstream", {})
    return {
        "method": (
            "observation-bag probe of CeTTa --profile he-compat --lang he "
            "against upstream Hyperon Experimental metta 0.2.10"
        ),
        "classification": row.get("classification", ""),
        "cetta": {
            "returncode": cetta.get("returncode"),
            "timed_out": cetta.get("timed_out"),
            "sha256": cetta.get("sha256"),
            "bytes": cetta.get("bytes"),
        },
        "upstream": {
            "returncode": upstream.get("returncode"),
            "timed_out": upstream.get("timed_out"),
            "sha256": upstream.get("sha256"),
            "bytes": upstream.get("bytes"),
        },
    }


def promote_broad_agree_case(row: dict[str, Any]) -> dict[str, Any]:
    path = str(row.get("path", ""))
    case_path = ROOT / path
    exp_path = expected_path_for(case_path)
    manifest = row.get("manifest", {})
    return enrich_case_with_rule_map(
        {
            "id": stable_id("broad-he-manifest", path),
            "tier": 3,
            "tier_name": "tier3-broad-runnable-corpus-agree",
            "source_kind": "broad_manifest_agree",
            "feature": "broad_runnable_corpus",
            "spec_refs": [first_ref_line(case_path)] if case_path.exists() and first_ref_line(case_path) else [],
            "lean_authority": [],
            "cetta_path": path,
            "cetta_expected_path": rel(exp_path) if exp_path.exists() else "",
            "cetta_result": result_snapshot(exp_path),
            "manifest": manifest,
            "upstream_oracle": {
                "engine": "Hyperon Experimental metta",
                "version": "0.2.10",
                "repository_head_ref": "repositories.hyperon_experimental.head",
                "mode": "broad-runnable-corpus-probe",
            },
            "lean_comparator": {},
            "comparison_evidence": compact_probe_evidence(row),
            "classification": "agree",
            "notes": (
                "Promoted from broad runnable-corpus probe because live CeTTa "
                "he-compat output matched live upstream Rust HE output."
            ),
        }
    )


def build_broad_probe_summary(report_path: Path, cases: list[dict[str, Any]]) -> dict[str, Any]:
    if not report_path.exists():
        return {
            "status": "missing",
            "report_path": str(report_path),
            "promoted_case_count": 0,
            "triage": [],
        }
    data = json.loads(report_path.read_text(encoding="utf-8"))
    results = data.get("results", [])
    if not isinstance(results, list):
        return {
            "status": "invalid",
            "report_path": str(report_path),
            "promoted_case_count": 0,
            "triage": [],
        }

    existing_paths = {str(row.get("cetta_path", "")) for row in cases}
    triage: list[dict[str, Any]] = []
    category_counts: Counter[str] = Counter()
    classification_counts: Counter[str] = Counter()
    promoted = 0

    for result in results:
        if not isinstance(result, dict):
            continue
        path = str(result.get("path", ""))
        classification = str(result.get("classification", ""))
        category = triage_broad_result(result)
        category_counts[category] += 1
        classification_counts[classification] += 1
        rule_ids = broad_rule_ids(result, category)
        details = BROAD_CATEGORY_DETAILS.get(category, {})

        if classification == "agree" and path not in existing_paths:
            cases.append(promote_broad_agree_case(result))
            existing_paths.add(path)
            promoted += 1

        entry: dict[str, Any] = {
            "path": path,
            "classification": classification,
            "category": category,
            "category_kind": details.get("kind", "unknown"),
            "recommended_action": details.get("recommended_action", ""),
            "spec_rule_ids": rule_ids,
            "manifest_lane": result.get("manifest", {}).get("lane", ""),
            "manifest_notes": result.get("manifest", {}).get("notes", ""),
        }
        if classification != "agree":
            entry["cetta_head"] = str(result.get("cetta", {}).get("head", ""))[:500]
            entry["upstream_head"] = str(result.get("upstream", {}).get("head", ""))[:500]
        if path in BROAD_RUST_DRIFT_NOTES:
            drift = BROAD_RUST_DRIFT_NOTES[path]
            entry["rust_drift_note"] = drift.get("note", "")
            entry["rust_drift_spec_refs"] = drift.get("spec_refs", [])
        if path in BROAD_MODULE_RESOLUTION_NOTES:
            module_note = BROAD_MODULE_RESOLUTION_NOTES[path]
            entry["module_resolution_note"] = module_note.get("note", "")
            entry["module_resolution_spec_refs"] = module_note.get("spec_refs", [])
        if path in BROAD_MODULE_INVENTORY_NOTES:
            inventory_note = BROAD_MODULE_INVENTORY_NOTES[path]
            entry["module_inventory_note"] = inventory_note.get("note", "")
            entry["module_inventory_spec_refs"] = inventory_note.get("spec_refs", [])
        triage.append(entry)

    local_library_rows = [
        f"{result.get('path', '')}: {str(result.get('upstream', {}).get('head', ''))[:200]}"
        for result in results
        if isinstance(result, dict)
        and triage_broad_result(result) == "local-library-or-import-extension"
        and not upstream_module_resolution_failure(str(result.get("upstream", {}).get("head", "")))
    ]
    if local_library_rows:
        details = "\n  - ".join(local_library_rows)
        raise SystemExit(
            "local-library/import extension bucket contains rows where Rust "
            f"did not stop at the module boundary:\n  - {details}"
        )

    unnoted_module_resolution_rows = [
        row["path"]
        for row in triage
        if row["category"] == "module-resolution-diff"
        and row["path"] not in BROAD_MODULE_RESOLUTION_NOTES
    ]
    if unnoted_module_resolution_rows:
        details = "\n  - ".join(sorted(unnoted_module_resolution_rows))
        raise SystemExit(
            "module-resolution rows require explicit policy notes:\n"
            f"  - {details}"
        )

    unnoted_module_inventory_rows = [
        row["path"]
        for row in triage
        if row["category"] == "module-inventory-environment-diff"
        and row["path"] not in BROAD_MODULE_INVENTORY_NOTES
    ]
    if unnoted_module_inventory_rows:
        details = "\n  - ".join(sorted(unnoted_module_inventory_rows))
        raise SystemExit(
            "module-inventory rows require explicit policy notes:\n"
            f"  - {details}"
        )

    closed_rows = [
        f"{row['path']} -> {row['category']}"
        for row in triage
        if row["category"] in CLOSED_BROAD_CATEGORIES
    ]
    if closed_rows:
        details = "\n  - ".join(closed_rows)
        raise SystemExit(f"closed HE-compat broad buckets reappeared:\n  - {details}")

    serious_categories = {
        "module-resolution-diff",
        "module-inventory-environment-diff",
        "nondet-order-or-collapse-shape",
        "core-builtin-or-result-shape-gap",
        "cetta-timeout",
    }
    return {
        "status": "loaded",
        "report_path": str(report_path),
        "source_summary": data.get("summary", {}),
        "promoted_case_count": promoted,
        "triage_count": len(triage),
        "by_classification": dict(sorted(classification_counts.items())),
        "by_category": dict(sorted(category_counts.items())),
        "serious_triage_count": sum(category_counts[cat] for cat in serious_categories),
        "category_details": BROAD_CATEGORY_DETAILS,
        "triage": triage,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument(
        "--broad-probe-report",
        type=Path,
        default=(
            ROOT
            / "tests"
            / "generated"
            / "he_compat"
            / f"he_compat_broad_corpus_{CATALOG_DATE}.json"
        ),
        help=(
            "optional broad runnable-corpus probe report; when present, "
            "observed-agree cases are promoted as Tier 3 evidence and diffs "
            "are summarized as triage data"
        ),
    )
    args = parser.parse_args()

    validate_broad_rust_drift_notes()
    validate_broad_module_resolution_notes()
    validate_broad_module_inventory_notes()
    validate_track_a_rule_lanes()
    cases = build_cases()
    broad_probe = build_broad_probe_summary(args.broad_probe_report, cases)
    payload = {
        "schema_version": 1,
        "catalog_date": CATALOG_DATE,
        "subject": "CeTTa HE-compatible runtime surface",
        "no_runtime_trace_or_certificate_surface": True,
        "semantic_indexing": {
            "unit": "official-spec rule/capacity",
            "status": (
                "Every catalog case is mapped to dated spec-rule IDs. The current "
                "mapping is source/feature-derived; the next strengthening is to "
                "replace heuristic case maps with Lean-exported HELanguageDef rule "
                "evidence."
            ),
            "track_a_relation_lanes": {
                "reduction-span": (
                    "Current Track A / NTT-relevant reduction edges. These are the "
                    "rule capacities intended to line up with LanguageDef/langReduces/"
                    "langDiamond-style reasoning."
                ),
                "supporting-surface": (
                    "Evaluator/runtime support capacities that inform the object-language "
                    "story but are not part of the current core reduction-span slice."
                ),
                "spatial-policy": (
                    "Spatial/policy/meta capacities intentionally kept out of the current "
                    "reduction span. These remain visible in the catalog so policy/evidence "
                    "does not masquerade as runtime semantics."
                ),
            },
            "oslf_pipeline": {
                "language_def": OSLF_ANCHORS["language_def"],
                "rewrite_to_modal": OSLF_ANCHORS["type_synthesis"],
                "premise_reduction": OSLF_ANCHORS["premise_reduction"],
            },
        },
        "profile_policy": {
            "formal_he": {
                "scope": (
                    "Spec-faithful HE interpretation used as the formal base for "
                    "OSLF/NTT/DTT work. This is the default --lang he path and "
                    "is also available explicitly as --profile he."
                ),
                "eval_surface_sentence": (
                    "When the eval instruction reaches a non-reducible expression E, "
                    "the user-visible formal-HE result is E itself."
                ),
                "doc_surface_sentence": (
                    "get-doc and help! follow the documented stdlib documentation "
                    "surface for the active language/profile; extension-only "
                    "documented surfaces are hidden. Rust HE 0.2.10 get-doc "
                    "arity failures are reproduced only by he-compat."
                ),
            },
            "he_compat": {
                "scope": (
                    "Literal compatibility catalog for upstream Hyperon Experimental "
                    "HE 0.2.10. This lane intentionally reproduces observed Rust HE "
                    "surface quirks; formal/spec-faithful behavior belongs to --lang "
                    "he / --profile he."
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
        "broad_probe": broad_probe,
        "spec_rule_capacities": spec_rule_capacities(),
        "cases": cases,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"wrote {args.out} ({len(cases)} cases)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
