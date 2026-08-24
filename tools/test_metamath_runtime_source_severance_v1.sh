#!/usr/bin/env bash

set -euo pipefail

: "${CETTA_DIRECT_PETTA_ROOT:?CETTA_DIRECT_PETTA_ROOT is required}"
: "${CETTA_DIRECT_PETTA_BIN:?CETTA_DIRECT_PETTA_BIN is required}"
: "${CETTA_DIRECT_PETTA_COMPILER:?CETTA_DIRECT_PETTA_COMPILER is required}"
: "${CETTA_DIRECT_PETTA_CURSOR_COMPILER:?CETTA_DIRECT_PETTA_CURSOR_COMPILER is required}"
: "${CETTA_DIRECT_PETTA_CURSOR_PREFIX:?CETTA_DIRECT_PETTA_CURSOR_PREFIX is required}"
: "${CETTA_DIRECT_PETTA_RULE_MUTATOR:?CETTA_DIRECT_PETTA_RULE_MUTATOR is required}"
: "${CETTA_DIRECT_PETTA_PROGRAM:?CETTA_DIRECT_PETTA_PROGRAM is required}"
: "${CETTA_DIRECT_PETTA_QUERY:?CETTA_DIRECT_PETTA_QUERY is required}"
: "${CETTA_DIRECT_PETTA_MANIFEST:?CETTA_DIRECT_PETTA_MANIFEST is required}"
: "${CETTA_DIRECT_PETTA_REGULAR_DIGEST:?CETTA_DIRECT_PETTA_REGULAR_DIGEST is required}"
: "${CETTA_DIRECT_PETTA_GUARDED_DIGEST:?CETTA_DIRECT_PETTA_GUARDED_DIGEST is required}"
: "${CETTA_DIRECT_PETTA_ACTION_DIGEST:?CETTA_DIRECT_PETTA_ACTION_DIGEST is required}"
: "${CETTA_DIRECT_PETTA_SPAN_MASK_DIGEST:?CETTA_DIRECT_PETTA_SPAN_MASK_DIGEST is required}"
: "${CETTA_DIRECT_PETTA_SOURCE_CONTROL_DIGEST:?CETTA_DIRECT_PETTA_SOURCE_CONTROL_DIGEST is required}"

root=$(realpath "$CETTA_DIRECT_PETTA_ROOT")
runtime_bin=$(realpath "$CETTA_DIRECT_PETTA_BIN")
compiler=$(realpath "$CETTA_DIRECT_PETTA_COMPILER")
cursor_compiler=$(realpath "$CETTA_DIRECT_PETTA_CURSOR_COMPILER")
cursor_prefix=$CETTA_DIRECT_PETTA_CURSOR_PREFIX
mutator=$(realpath "$CETTA_DIRECT_PETTA_RULE_MUTATOR")
program=$(realpath "$CETTA_DIRECT_PETTA_PROGRAM")
query=$(realpath "$CETTA_DIRECT_PETTA_QUERY")
manifest=$(realpath "$CETTA_DIRECT_PETTA_MANIFEST")
manifest_name=$(basename "$manifest")
pack_relative=$(sed -n \
    's/^[[:space:]]*(parser-pack "\([^"]*\)").*$/\1/p' \
    "$manifest")
cursor_relative=$(sed -n \
    's/^[[:space:]]*(compiled-cursor "\([^"]*\)").*$/\1/p' \
    "$manifest")
lock_relative=$(sed -n \
    's/^[[:space:]]*(lock "\([^"]*\)").*$/\1/p' \
    "$manifest")
for relative in "$pack_relative" "$cursor_relative" "$lock_relative"; do
    if [[ -z "$relative" || "$relative" = /* ||
            "$relative" = *'..'* ]]; then
        echo 'direct runtime manifest contains an invalid artifact path' >&2
        exit 2
    fi
done
driver="$root/tools/langdef_direct_petta_outcome_v1.sh"
input="$root/tests/langdef/metamath/include/basic_root.mm"
work=$(mktemp -d "$root/langdef/metamath-runtime-severance.XXXXXX")
trap 'rm -rf "$work"' EXIT INT TERM

run_driver() {
    local selected_manifest=$1
    CETTA_DIRECT_PETTA_ROOT="$root" \
        CETTA_DIRECT_PETTA_BIN="$runtime_bin" \
        CETTA_DIRECT_PETTA_PROGRAM="$program" \
        CETTA_DIRECT_PETTA_QUERY="$query" \
        CETTA_DIRECT_PETTA_MANIFEST="$selected_manifest" \
        CETTA_DIRECT_PETTA_TIMEOUT=120 \
        "$driver" "$input"
}

canonical=$(run_driver "$manifest")
if [[ "$canonical" != \
        '(MetamathDirectPeTTaOutcomeV1 AcceptedV1)' ]]; then
    echo 'canonical direct include route did not accept its witness' >&2
    exit 1
fi
program_digest_before=$(sha256sum "$program" | cut -d' ' -f1)

cp -R "$root/langdef/metamath/." "$work/"
mutated_cursor="$work/$cursor_relative"
mutated_cursor_c="${mutated_cursor%.so}.c"
mutated_pack="$work/$pack_relative"
mutated_lock="$work/$lock_relative"
mutated_manifest="$work/$manifest_name"
mkdir -p "$(dirname "$mutated_cursor")" "$(dirname "$mutated_lock")"
"$mutator" mutate \
    --source "$root/langdef/metamath/state_v1.metta" \
    --out "$work/state_v1.metta" \
    --rule mm-state-include-resolve --mode delete

"$compiler" petta-direct \
    --source "$root/experiments/gslt2parse_foundation/presentations/core/occurrence_fold_core_v1.metta" \
    --source "$root/langdef/metamath/source_fold_v1.metta" \
    --source "$root/experiments/gslt2parse_foundation/presentations/core/relational_state_program_core_v1.metta" \
    --source "$work/state_v1.metta" \
    --source "$root/langdef/metamath/proof_trace_policy_v1.metta" \
    --source "$root/langdef/metamath/proof_v1.metta" \
    --source "$root/experiments/gslt2parse_foundation/presentations/compiler/relational_state_program_compiler_v1.metta" \
    --source "$root/experiments/gslt2parse_foundation/presentations/core/relational_state_program_interface_v1.metta" \
    --out "$work/state-compiler.metta" >/dev/null
awk '1' "$work/state-compiler.metta" \
    "$root/tests/petta/metamath_state_compiler_direct_query_v1.metta" \
    >"$work/state-compiler-run.metta"
timeout 120 env CETTA_PETTA_SEARCH_MACHINE=1 \
    "$runtime_bin" --lang petta "$work/state-compiler-run.metta" \
    >"$work/state.raw" 2>"$work/state.stderr"
LC_ALL=C sort -u "$work/state.raw" >"$work/state.answers"
test "$(wc -l <"$work/state.answers")" -eq 104
if grep -Fq '(state-action-v1 mm-resolve-include ' \
        "$work/state.answers"; then
    echo 'severed authored rule survived state compilation' >&2
    exit 1
fi

"$cursor_compiler" \
    --abi "$root/langdef/metamath/generated/normalized_parser_pack_v1.abi" \
    --lexical-answers "$root/langdef/metamath/generated/lexical_nfa_v1.answers" \
    --guard-answers "$root/langdef/metamath/generated/empty_guard_v1.answers" \
    --guard-evidence "$root/langdef/metamath/generated/empty_guard_v1.evidence" \
    --guarded-answers "$root/langdef/metamath/generated/empty_guard_v1.answers" \
    --regular-compiler-digest "$CETTA_DIRECT_PETTA_REGULAR_DIGEST" \
    --guarded-compiler-digest "$CETTA_DIRECT_PETTA_GUARDED_DIGEST" \
    --action-answers "$root/langdef/metamath/generated/action_bytecode_v1.answers" \
    --action-compiler-digest "$CETTA_DIRECT_PETTA_ACTION_DIGEST" \
    --occurrence-fold-answers "$root/langdef/metamath/generated/occurrence_fold_v1.answers" \
    --occurrence-span-mask-answers "$root/langdef/metamath/generated/occurrence_span_mask_v1.answers" \
    --occurrence-span-mask-compiler-digest "$CETTA_DIRECT_PETTA_SPAN_MASK_DIGEST" \
    --state-answers "$work/state.answers" \
    --source-control-answers "$root/langdef/metamath/generated/source_resolution_control_direct_v1.answers" \
    --source-control-compiler-digest "$CETTA_DIRECT_PETTA_SOURCE_CONTROL_DIGEST" \
    --out-c "$mutated_cursor_c" \
    --prefix "$cursor_prefix" >/dev/null

make -C "$root" --no-print-directory BUILD=core ENABLE_LIB_PROLOG=0 \
    build-langdef-compiled-cursor-generated-v1 \
    GENERATED_LANGDEF_CURSOR_C="$mutated_cursor_c" \
    GENERATED_LANGDEF_CURSOR_SO="$mutated_cursor" \
    GENERATED_LANGDEF_CURSOR_PREFIX="$cursor_prefix" \
    >/dev/null

"$compiler" seal \
    --manifest "$mutated_manifest" \
    --pack "$mutated_pack" \
    --compiled-cursor "$mutated_cursor" \
    --lock-out "$mutated_lock" >/dev/null

severed=$(run_driver "$mutated_manifest")
if [[ "$severed" != \
        '(MetamathDirectPeTTaOutcomeV1 MissingSemanticOutcomeV1)' ]]; then
    echo 'severed runtime source package did not fail closed' >&2
    printf '%s\n' "$severed" >&2
    exit 1
fi
program_digest_after=$(sha256sum "$program" | cut -d' ' -f1)
test "$program_digest_before" = "$program_digest_after"

printf '%s\n' \
    '(MetamathDirectSourceSeveranceV1Summary 1 105 104 1 1 0)'
