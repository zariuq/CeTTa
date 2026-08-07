#!/usr/bin/env bash
set -euo pipefail

cetta=${CETTA_BIN:-./cetta}
reference=${PETTA_TYPECHECK_REFERENCE_ROOT:-}
timeout_seconds=${PETTA_TYPECHECK_TIMEOUT_SECONDS:-30}

if [[ -z "$reference" || ! -d "$reference/examples" ]]; then
    echo "set PETTA_TYPECHECK_REFERENCE_ROOT to the pinned PeTTa typecheck-v2 checkout" >&2
    exit 2
fi
cetta=$(realpath "$cetta")
reference=$(realpath "$reference")

run_reference_case() (
    cd "$reference"
    timeout "$timeout_seconds" "$cetta" "$@"
)

work=$(mktemp -d runtime/petta-typecheck-v2-focused.XXXXXX)
trap 'rm -rf "$work"' EXIT INT TERM

if ! "$cetta" --lang petta --list-profiles |
     awk -F '\t' '$1 == "typecheck-v2" { found = 1 } END { exit !found }'; then
    echo "FAIL: typecheck-v2 profile inventory" >&2
    exit 1
fi

default_stdout="$work/default-petta-catch.stdout"
default_stderr="$work/default-petta-catch.stderr"
if ! CETTA_PETTA_SEARCH_MACHINE=1 timeout "$timeout_seconds" \
        "$cetta" --lang petta \
        tests/petta/search_machine_catch_cons.metta \
        >"$default_stdout" 2>"$default_stderr" ||
   ! cmp -s "$default_stdout" \
        tests/petta/search_machine_catch_cons.expected ||
   [[ -s "$default_stderr" ]]; then
    echo "FAIL: default PeTTa catch/error noninterference" >&2
    diff -u tests/petta/search_machine_catch_cons.expected \
        "$default_stdout" | head -40 >&2 || true
    sed -n '1,20p' "$default_stderr" >&2
    exit 1
fi

obligation_stdout="$work/deferred-obligations.stdout"
obligation_stderr="$work/deferred-obligations.stderr"
if ! timeout "$timeout_seconds" "$cetta" --lang petta \
        --profile typecheck-v2 \
        tests/petta/typecheck_v2_deferred_obligations.metta \
        >"$obligation_stdout" 2>"$obligation_stderr" ||
   ! cmp -s "$obligation_stdout" \
        tests/petta/typecheck_v2_deferred_obligations.expected ||
   [[ -s "$obligation_stderr" ]]; then
    echo "FAIL: deferred type obligations and rollback" >&2
    diff -u tests/petta/typecheck_v2_deferred_obligations.expected \
        "$obligation_stdout" | head -40 >&2 || true
    sed -n '1,20p' "$obligation_stderr" >&2
    exit 1
fi

authority_negative=tests/petta/typecheck_v2_repros/02_stale_obligation_conflicting_type_addition.metta
set +e
timeout "$timeout_seconds" "$cetta" --lang petta \
    --profile typecheck-v2 "$authority_negative" \
    >"$work/authority-negative.stdout" \
    2>"$work/authority-negative.stderr"
authority_status=$?
set -e
if [[ $authority_status -ne 2 ]] ||
   [[ -s "$work/authority-negative.stdout" ]] ||
   ! grep -Fq 'PeTTa type error:' "$work/authority-negative.stderr"; then
    echo "FAIL: Space mutation did not withdraw a cached type obligation" >&2
    exit 1
fi

for authority_stem in \
    11_stale_obligation_nonconflicting_addition \
    12_obligation_transaction_rollback \
    13_obligation_catch_rollback \
    18_obligation_deterministic_heap_relocation; do
    authority_source="tests/petta/typecheck_v2_repros/$authority_stem.metta"
    authority_expected="tests/petta/typecheck_v2_repros/$authority_stem.expected"
    authority_stdout="$work/$authority_stem.stdout"
    authority_stderr="$work/$authority_stem.stderr"
    if ! timeout "$timeout_seconds" "$cetta" --lang petta \
            --profile typecheck-v2 "$authority_source" \
            >"$authority_stdout" 2>"$authority_stderr" ||
       ! cmp -s "$authority_expected" "$authority_stdout" ||
       [[ -s "$authority_stderr" ]]; then
        echo "FAIL: type-obligation authority/rollback witness $authority_stem" >&2
        exit 1
    fi
done

negatives=(
    fail_case_output_mismatch
    fail_catch_output_mismatch
    fail_collapse_mixed_list
    fail_collapse_output_mismatch
    fail_declared_sentinel_param
    fail_destructured_param_mismatch
    fail_det_unwrappable_list_builtin
    fail_det_field_in_and
    fail_eval_quote_output_mismatch
    fail_foldall_output_mismatch
    fail_forall_output_mismatch
    fail_hyperpose_output_mismatch
    fail_import_syntax_error
    fail_import_type_error
    fail_inferred_literal_mismatch
    fail_infix_arrow
    fail_library_unverified_result
    fail_list_type_mismatch
    fail_map_output_mismatch
    fail_mutex_output_mismatch
    fail_nested_parametric_param
    fail_non_parametric_output
    fail_nonparametric_param
    fail_once_output_mismatch
    fail_progn_output_mismatch
    fail_quoted_compound_output
    fail_reduce_output_mismatch
    fail_runnable_callsite_literal_mismatch
    fail_runtime_noncallable_arrow
    fail_sealed_output_mismatch
    fail_single_poly_compile_mismatch
    fail_specialized_output_guard
    fail_superpose_output_mismatch
    fail_transaction_output_mismatch
    fail_union_type_mismatch
    fail_unbound_det_argument
)

for stem in "${negatives[@]}"; do
    source_file="$reference/examples/$stem.metta"
    stdout_file="$work/$stem.stdout"
    stderr_file="$work/$stem.stderr"
    set +e
    run_reference_case --lang petta \
        --profile typecheck-v2 "$source_file" \
        >"$stdout_file" 2>"$stderr_file"
    status=$?
    set -e
    if [[ $status -ne 2 ]] ||
       ! grep -Fq 'PeTTa type error:' "$stderr_file" ||
       [[ -s "$stdout_file" ]]; then
        echo "FAIL: $stem was not a clean native type rejection" >&2
        echo "exit=$status" >&2
        sed -n '1,20p' "$stdout_file" >&2
        sed -n '1,20p' "$stderr_file" >&2
        exit 1
    fi
done

mode_negatives=(
    'fail_late_effect_conditional|'
    'fail_late_output_cert_consumer|'
    'fail_spaceof_existing_row|'
    'fail_stale_transitive_det_recompile|'
    'fail_strict_branch_unknown_arg|--strict'
    'fail_strict_branch_unknown_output|--strict'
    'fail_strict_collapse_unknown_elem|--strict'
    'fail_strict_contextual_list|--strict'
    'fail_strict_deferred_guard_bad_binding|--strict'
    'fail_strict_foldall_unknown_initializer|--strict'
    'fail_strict_library_trusted_residual|--strict'
    'fail_strict_quoted_structural_unknown|--strict'
    'fail_strict_runtime_guard|--strict'
    'fail_strict_union_undefined_second_ctor|--strict'
    'fail_strict_union_var_branch_first|--strict'
    'fail_strict_alias_removal_space_dependency|--strict'
    'fail_strict_late_space_schema|--strict'
    'fail_strict_match_contextual_result|--strict'
    'fail_strict_newtype_pattern_literal|--strict'
    'fail_strict_space_schema_removal|--strict'
    'fail_strict_typed_space_at_pattern|--strict'
    'fail_strict_typed_space|--strict'
    'fail_strictdet_addatom|--strict-det'
    'fail_strictdet_if_equal_incomplete_narrowing|--strict-det'
    'fail_strictdet_late_case_fallthrough_constructor|--strict-det'
    'fail_strictdet_map_open_list|--strict-det'
    'fail_strictdet_nondet_builtin_closure|--strict-det'
)
for entry in "${mode_negatives[@]}"; do
    stem=${entry%%|*}
    mode=${entry#*|}
    source_file="$reference/examples/$stem.metta"
    stdout_file="$work/$stem.stdout"
    stderr_file="$work/$stem.stderr"
    args=(--lang petta --profile typecheck-v2)
    if [[ -n "$mode" ]]; then
        args+=("$mode")
    fi
    set +e
    run_reference_case "${args[@]}" "$source_file" \
        >"$stdout_file" 2>"$stderr_file"
    status=$?
    set -e
    if [[ $status -ne 2 ]] ||
       ! grep -Fq 'PeTTa type error:' "$stderr_file" ||
       [[ -s "$stdout_file" ]]; then
        echo "FAIL: $stem was not a clean profile-aware type rejection" >&2
        echo "exit=$status" >&2
        sed -n '1,20p' "$stdout_file" >&2
        sed -n '1,20p' "$stderr_file" >&2
        exit 1
    fi
done

mapfile -t positives < <(
    find "$reference/examples" -maxdepth 1 -type f \
        \( -name 'strict_*.metta' -o -name 'strictdet_*.metta' \) \
        -printf '%f\n' | sort
)
if [[ ${#positives[@]} -ne 97 ]]; then
    echo "FAIL: pinned positive census is ${#positives[@]}, expected 97" >&2
    exit 1
fi

extended_pass=0
typecheck_pass=0
new_type_rejections=0
for name in "${positives[@]}"; do
    source_file="$reference/examples/$name"
    base_out="$work/$name.extended.stdout"
    base_err="$work/$name.extended.stderr"
    typed_out="$work/$name.typecheck.stdout"
    typed_err="$work/$name.typecheck.stderr"

    set +e
    run_reference_case --lang petta \
        --profile extended "$source_file" >"$base_out" 2>"$base_err"
    base_status=$?
    typed_args=(--lang petta --profile typecheck-v2)
    case "$name" in
        strictdet_*) typed_args+=(--strict-det) ;;
        strict_*) typed_args+=(--strict) ;;
    esac
    run_reference_case "${typed_args[@]}" \
        "$source_file" >"$typed_out" 2>"$typed_err"
    typed_status=$?
    set -e

    base_green=0
    typed_green=0
    if [[ $base_status -eq 0 ]] &&
       ! grep -Fq '❌' "$base_out" &&
       ! grep -Fq '❌' "$base_err" &&
       ! grep -Fq '(Error' "$base_out" &&
       ! grep -Fq '(Error' "$base_err"; then
        base_green=1
        ((extended_pass += 1))
    fi
    if [[ $typed_status -eq 0 ]] &&
       ! grep -Fq '❌' "$typed_out" &&
       ! grep -Fq '❌' "$typed_err" &&
       ! grep -Fq '(Error' "$typed_out" &&
       ! grep -Fq '(Error' "$typed_err"; then
        typed_green=1
        ((typecheck_pass += 1))
    fi
    if grep -Fq 'PeTTa type error:' "$typed_err"; then
        ((new_type_rejections += 1))
        echo "unexpected positive rejection: $name" >&2
    fi
done

printf 'PeTTa typecheck-v2 positives: extended-control=%d/97 typecheck-v2=%d/97 new-type-rejections=%d\n' \
    "$extended_pass" "$typecheck_pass" "$new_type_rejections"

if [[ $typecheck_pass -ne 97 || $new_type_rejections -ne 0 ]]; then
    exit 1
fi

echo "PASS: native PeTTa typecheck-v2 positive corpus and default-profile noninterference"
