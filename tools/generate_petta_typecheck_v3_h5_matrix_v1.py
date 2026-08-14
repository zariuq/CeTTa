#!/usr/bin/env python3
"""Generate the candidate-complete H5 target and witness matrix."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Sequence


SCHEMA = "cetta-petta-typecheck-v3-h5-matrix-v1"
INTAKE_SCHEMA = "cetta-petta-typecheck-v3-intake-v1"


CLASS_WITNESSES: dict[str, tuple[str, ...]] = {
    "selection-source-opacity": ("v3-selection-expression",),
    "selection-source-shape": ("v3-selection-expression",),
    "selection-open-expression-domain": ("v3-selection-expression",),
    "inferred-output-publication": ("joinPrecise", "v3-evidence-outcome-established"),
    "inferred-value-candidate": ("v3-evidence-outcome-established",),
    "foldall-empty-generator": ("v3-fold-empty",),
    "foldall-unresolved-generator": ("v3-evidence-outcome-established",),
    "inferred-conditional-no-result": ("v3-branch-some-none",),
    "conditional-proper-list-selection": ("v3-conditional-evidence", "v3-selection-list"),
    "case-proper-list-selection": ("v3-branch-some-some", "v3-selection-list"),
    "identity-binding-proper-list-selection": ("v3-binding-apply-cons", "v3-selection-list"),
    "identity-binding-nonlist-rejection": ("v3-binding-apply-cons", "v3-selection-list"),
    "sequential-identity-binding-proper-list-selection": ("v3-binding-apply-cons", "v3-selection-list"),
    "chain-pattern-binding-semidet": ("v3-binding-apply-cons", "V3ModeFits"),
    "identity-binding-bound-bool-selection": ("v3-binding-apply-cons", "v3-selection-bool"),
    "identity-binding-nonbool-rejection": ("v3-binding-apply-cons", "v3-selection-bool"),
    "sequential-identity-binding-bound-bool-selection": ("v3-binding-apply-cons", "v3-selection-bool"),
    "administrative-alias-chain-bound-bool-selection": ("v3-binding-resolve-alias", "v3-selection-bool"),
    "administrative-alias-chain-nonbool-rejection": ("v3-binding-resolve-alias", "v3-selection-bool"),
    "administrative-alias-chain-proper-list-selection": ("v3-binding-resolve-alias", "v3-selection-list"),
    "administrative-alias-chain-nondet-preservation": ("v3-binding-resolve-alias", "V3ModeFits"),
    "administrative-alias-chain-length-preservation": ("v3-binding-resolve-alias", "v3-selection-bool"),
    "administrative-alias-lexical-shadowing": ("v3-binding-lookup-succ", "v3-selection-bool"),
    "all-bottom-inference-withholds-signature": ("v3-branch-none-none",),
    "declared-all-bottom-semidet": ("v3-branch-none-none", "V3ModeFits"),
    "all-bottom-case-inference-withholds-signature": ("v3-branch-none-none",),
    "declared-all-bottom-case-semidet": ("v3-branch-none-none", "V3ModeFits"),
    "empty-alternative-set-inference-withholds-signature": ("v3-all-empty-superpose",),
    "superpose-empty-alternatives-retain-nondet": ("v3-all-empty-superpose", "V3ModeFits"),
    "superpose-empty-alternatives-nondet-accept": ("v3-all-empty-superpose", "V3ModeFits"),
    "quoted-inner-type-form-data": ("v3-expression-evidence-quote",),
    "quoted-inner-data-form-code": (
        "v3-expression-evidence-quoted-product",
        "v3-consistent-product",
    ),
    "dynamic-eval-committed-grade-reject": ("v3-expression-evidence-eval", "V3ModeFits"),
    "evaluated-brand-literal-reject": ("v3-expression-evidence-eval", "V3Consistent"),
    "dynamic-eval-nondet-admission": ("v3-expression-evidence-eval", "V3ModeFits"),
    "literal-newtype-introduction-reject": ("v3-expression-evidence-known", "V3Consistent"),
    "nested-structured-selection-evidence-withheld": ("v3-selection-expression",),
    "open-head-selection-withholds-deterministic-grade": ("v3-selection-expression",),
    "once-cardinality-transformer": ("v3-once-nondet",),
}


TARGETS: dict[str, str] = {
    "71_concrete_data_selection_is_deterministic|strict-det": "established",
    "72_direct_concrete_selection_is_deterministic|strict-det": "established",
    "73_expression_empty_cons_selection_is_not_total|strict-det": "refuted-cardinality",
    "74_inferred_superpose_result_widens_unknown|default": "established",
    "74_inferred_superpose_result_widens_unknown|strict": "established",
    "75_inferred_superpose_unknown_does_not_reject|default": "refuted-shape",
    "76_inferred_canonical_list_preserved|strict": "established",
    "77_inferred_canonical_list_rejects_mismatch|strict": "refuted-shape",
    "78_inferred_canonical_arrow_preserved|strict": "established",
    "79_inferred_canonical_arrow_rejects_mismatch|strict": "refuted-shape",
    "80_inferred_ambiguous_symbol_widens_unknown|strict": "refuted-shape",
    "81_inferred_untyped_atom_has_no_positive_type|strict": "undetermined",
    "82_inferred_function_is_not_explicit_value_evidence|strict": "undetermined",
    "84_foldall_empty_returns_initializer|strict-det": "established",
    "85_foldall_disguised_empty_reject|strict": "refuted-shape",
    "87_inferred_if_empty_result|strict": "established",
    "89_if_nonempty_list_selection|strict-det": "established",
    "90_case_proper_list_selection|strict-det": "established",
    "91_let_bound_proper_list_selection|strict-det": "established",
    "92_let_nonlist_selection_reject|strict-det": "refuted-shape",
    "93_let_star_bound_proper_list_selection|strict-det": "established",
    "94_chain_identity_proper_list_selection|strict-det": "refuted-cardinality",
    "95_let_bound_bool_selection|strict-det": "established",
    "96_let_nonbool_bool_selection_reject|strict-det": "refuted-shape",
    "97_let_star_bound_bool_selection|strict-det": "established",
    "98_let_star_alias_chain_bool_selection|strict-det": "established",
    "99_let_star_alias_chain_nonbool_reject|strict-det": "refuted-shape",
    "100_let_star_alias_chain_proper_list_selection|strict-det": "established",
    "101_let_star_alias_chain_nondet_bool_reject|strict-det": "refuted-cardinality",
    "102_let_star_three_hop_bool_selection|strict-det": "established",
    "103_let_star_shadowed_alias_nonbool_reject|strict-det": "refuted-shape",
    "104_inferred_if_all_empty_result|strict": "undetermined",
    "105_declared_if_all_empty_semidet_accept|strict": "established",
    "106_inferred_case_all_empty_result|strict": "undetermined",
    "107_declared_case_all_empty_semidet_accept|strict": "established",
    "108_inferred_superpose_all_empty_result|strict": "undetermined",
    "109_declared_superpose_all_empty_semidet_accept|strict": "refuted-cardinality",
    "110_declared_superpose_all_empty_nondet_accept|strict": "established",
    "111_quoted_brand_declared_output_candidate|strict-det": "refuted-stage",
    "112_quoted_the_declared_output_candidate|strict-det": "refuted-stage",
    "113_quoted_data_marker_declared_product_candidate|strict-det": "refuted-shape",
    "114_quoted_data_marker_full_code_product_candidate|strict-det": "established",
    "115_eval_quoted_brand_reactivates_nominal_assertion_candidate|strict-det": "established",
    "116_eval_quoted_the_reactivates_ascription_candidate|strict-det": "established",
    "117_eval_quoted_brand_nondet_admission_candidate|strict-det": "refuted-shape",
    "118_eval_quoted_literal_nondet_candidate|strict-det": "established",
    "119_eval_quoted_the_nondet_candidate|strict-det": "established",
    "120_direct_brand_literal_newtype_candidate|strict-det": "established",
    "121_nested_literal_selector_totality_candidate|strict-det": "undetermined",
    "123_variable_headed_selection_literal_body_candidate|strict-det": "undetermined",
    "examples/determinism.metta|--strict-det": "established",
}


MIGRATIONS: dict[str, str] = {
    "71_concrete_data_selection_is_deterministic|strict-det": "Existing source becomes accepted; no edit.",
    "72_direct_concrete_selection_is_deterministic|strict-det": "Existing source becomes accepted; no edit.",
    "74_inferred_superpose_result_widens_unknown|strict": "Existing source becomes accepted by precise union publication; no edit.",
    "75_inferred_superpose_unknown_does_not_reject|default": "Widen the consumer to Number-or-String or add an explicit Number refinement.",
    "87_inferred_if_empty_result|strict": "Existing semideterministic source becomes accepted; no edit.",
    "115_eval_quoted_brand_reactivates_nominal_assertion_candidate|strict-det": "Existing source becomes accepted after explicit eval reactivates held evidence; no edit.",
    "116_eval_quoted_the_reactivates_ascription_candidate|strict-det": "Existing source becomes accepted after explicit eval reactivates held evidence; no edit.",
    "120_direct_brand_literal_newtype_candidate|strict-det": "Existing branded constructor becomes accepted; no edit.",
}


SOURCE_EXECUTABLE = {
    "71_concrete_data_selection_is_deterministic|strict-det",
    "72_direct_concrete_selection_is_deterministic|strict-det",
    "73_expression_empty_cons_selection_is_not_total|strict-det",
    "74_inferred_superpose_result_widens_unknown|default",
    "74_inferred_superpose_result_widens_unknown|strict",
    "75_inferred_superpose_unknown_does_not_reject|default",
    "76_inferred_canonical_list_preserved|strict",
    "77_inferred_canonical_list_rejects_mismatch|strict",
    "78_inferred_canonical_arrow_preserved|strict",
    "79_inferred_canonical_arrow_rejects_mismatch|strict",
    "80_inferred_ambiguous_symbol_widens_unknown|strict",
    "87_inferred_if_empty_result|strict",
    "121_nested_literal_selector_totality_candidate|strict-det",
    "123_variable_headed_selection_literal_body_candidate|strict-det",
    "examples/determinism.metta|--strict-det",
    "81_inferred_untyped_atom_has_no_positive_type|strict",
    "82_inferred_function_is_not_explicit_value_evidence|strict",
    "84_foldall_empty_returns_initializer|strict-det",
    "85_foldall_disguised_empty_reject|strict",
    "111_quoted_brand_declared_output_candidate|strict-det",
    "112_quoted_the_declared_output_candidate|strict-det",
    "113_quoted_data_marker_declared_product_candidate|strict-det",
    "114_quoted_data_marker_full_code_product_candidate|strict-det",
    "115_eval_quoted_brand_reactivates_nominal_assertion_candidate|strict-det",
    "116_eval_quoted_the_reactivates_ascription_candidate|strict-det",
    "117_eval_quoted_brand_nondet_admission_candidate|strict-det",
    "118_eval_quoted_literal_nondet_candidate|strict-det",
    "119_eval_quoted_the_nondet_candidate|strict-det",
    "120_direct_brand_literal_newtype_candidate|strict-det",
    "109_declared_superpose_all_empty_semidet_accept|strict",
    "110_declared_superpose_all_empty_nondet_accept|strict",
    "104_inferred_if_all_empty_result|strict",
    "105_declared_if_all_empty_semidet_accept|strict",
    "106_inferred_case_all_empty_result|strict",
    "107_declared_case_all_empty_semidet_accept|strict",
    "108_inferred_superpose_all_empty_result|strict",
    "89_if_nonempty_list_selection|strict-det",
    "90_case_proper_list_selection|strict-det",
    "91_let_bound_proper_list_selection|strict-det",
    "92_let_nonlist_selection_reject|strict-det",
    "93_let_star_bound_proper_list_selection|strict-det",
    "94_chain_identity_proper_list_selection|strict-det",
    "95_let_bound_bool_selection|strict-det",
    "96_let_nonbool_bool_selection_reject|strict-det",
    "97_let_star_bound_bool_selection|strict-det",
    "98_let_star_alias_chain_bool_selection|strict-det",
    "99_let_star_alias_chain_nonbool_reject|strict-det",
    "100_let_star_alias_chain_proper_list_selection|strict-det",
    "101_let_star_alias_chain_nondet_bool_reject|strict-det",
    "102_let_star_three_hop_bool_selection|strict-det",
    "103_let_star_shadowed_alias_nonbool_reject|strict-det",
}


def policy_verdict(target: str, candidate_id: str) -> str:
    if target == "established":
        return "accept"
    if target.startswith("refuted-"):
        return "reject"
    if target == "undetermined":
        return "reject" if "|strict" in candidate_id else "accept"
    raise ValueError(f"unknown native target {target}")


def build_matrix(intake: object) -> dict[str, object]:
    if not isinstance(intake, dict) or intake.get("schema") != INTAKE_SCHEMA:
        raise ValueError("unsupported v3 intake")
    native = intake.get("native_v3_intake")
    candidates = native.get("candidates") if isinstance(native, dict) else None
    if not isinstance(candidates, list):
        raise ValueError("v3 intake has no candidates")
    ids = {item.get("id") for item in candidates if isinstance(item, dict)}
    if ids != set(TARGETS):
        missing = sorted(ids - set(TARGETS))
        extra = sorted(set(TARGETS) - ids)
        raise ValueError(f"target coverage differs: missing={missing} extra={extra}")
    classes = {item.get("class") for item in candidates if isinstance(item, dict)}
    if classes != set(CLASS_WITNESSES):
        missing = sorted(classes - set(CLASS_WITNESSES))
        extra = sorted(set(CLASS_WITNESSES) - classes)
        raise ValueError(f"class witness coverage differs: missing={missing} extra={extra}")
    rows = []
    for candidate in sorted(candidates, key=lambda item: str(item["id"])):
        identity = candidate["id"]
        target = TARGETS[identity]
        v3_verdict = policy_verdict(target, identity)
        v2_verdict = candidate["v2_expected"]["verdict"]
        divergence = v3_verdict != v2_verdict
        if divergence != (identity in MIGRATIONS):
            raise ValueError(f"migration coverage mismatch for {identity}")
        rows.append({
            **candidate,
            "native_target": target,
            "v3_policy_verdict": v3_verdict,
            "core_witnesses": list(CLASS_WITNESSES[candidate["class"]]),
            "core_status": "mechanism-executable",
            "source_status": (
                "executable" if identity in SOURCE_EXECUTABLE
                else "pending-source-elaboration"
            ),
            "diverges_from_v2": divergence,
            "migration": MIGRATIONS.get(identity),
        })
    return {
        "schema": SCHEMA,
        "candidate_count": len(rows),
        "class_count": len(CLASS_WITNESSES),
        "core_mapped_count": len(rows),
        "source_executable_count": len(SOURCE_EXECUTABLE),
        "divergence_count": sum(row["diverges_from_v2"] for row in rows),
        "candidates": rows,
    }


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--intake", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    try:
        matrix = build_matrix(json.loads(args.intake.read_text(encoding="utf-8")))
        rendered = json.dumps(matrix, indent=2, sort_keys=True) + "\n"
        if args.check:
            if args.output.read_text(encoding="utf-8") != rendered:
                raise ValueError("generated H5 matrix differs from checked artifact")
        else:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(rendered, encoding="utf-8")
    except (OSError, json.JSONDecodeError, ValueError) as error:
        parser.exit(1, f"H5 matrix generation rejected: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
