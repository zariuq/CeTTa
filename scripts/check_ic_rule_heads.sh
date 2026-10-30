#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BIN=$(realpath "${1:-$ROOT/cetta}")
cd "$ROOT"
OUT=runtime/ic-rule-heads-control
mkdir -p "$OUT"
"$BIN" --lang petta tests/petta/ic_rule_heads.metta > "$OUT/controls.stdout" 2> "$OUT/controls.stderr"
test ! -s "$OUT/controls.stderr"
test "$(grep -c '^[(]RuleCheck .* true)$' "$OUT/controls.stdout")" -eq 18
! grep -q '^ConstructorBodyMustNotRun$' "$OUT/controls.stdout"

queries=(
 '(rule-a (stv 1.0 0.1) (stv 2.0 0.2) (stv 3.0 0.3))'
 '(rule-b (stv 2.0 0.1) (stv 1.0 0.2) (stv 1.0 0.3))'
 '(rule-d (stv -2 0.9))'
 '(rule-e (stv 3 0.9))'
 '(rule-d (stv (superpose (1 2)) 0.9))'
 '(rule-d (stv (superpose ()) 0.9))'
 '(rule-d (stv (let $ignored (println! InputEffect) 3) 0.9))'
 '(rule-d (stv 3 (let $ignored (println! ConfidenceEffect) 0.9)))'
 '(rule-alias (Box 2 2))'
 '(rule-alias (Box 2 3))'
 '(rule-a (stv (superpose (1.0 2.0)) 0.1) (stv 2.0 0.2) (stv 3.0 0.3))'
 '(rule-a (stv 1.0 0.1) (stv 2.0 0.2) (stv (superpose (1.0 2.0)) 0.3))'
 '(rule-d (quote (stv 3 (println! QuotedEffectMustNotRun))))'
)
for variant in original rewritten; do
    {
        cat "$OUT/$variant.metta"
        for i in "${!queries[@]}"; do
            printf '\n!(println! (RuleAnswer %s (collapse %s)))\n' "$i" "${queries[$i]}"
        done
    } > "$OUT/$variant-execution.metta"
    "$BIN" --lang petta "$OUT/$variant-execution.metta" > "$OUT/$variant.stdout" 2> "$OUT/$variant.stderr"
    test ! -s "$OUT/$variant.stderr"
    test "$(grep -c '^[(]RuleAnswer ' "$OUT/$variant.stdout")" -eq "${#queries[@]}"
    ! grep -q '^QuotedEffectMustNotRun$' "$OUT/$variant.stdout"
done
cmp "$OUT/original.stdout" "$OUT/rewritten.stdout"
for variant in original rewritten; do
    { cat "$OUT/$variant.metta"; printf '\n!(rule-d (quote (stv (+ 1 2) 0.9)))\n'; } > "$OUT/$variant-fault.metta"
    if "$BIN" --lang petta "$OUT/$variant-fault.metta" > "$OUT/$variant-fault.stdout" 2> "$OUT/$variant-fault.stderr"; then
        status=0
    else status=$?; fi
    printf '%s\n' "$status" > "$OUT/$variant-fault.status"
    test "$status" -ne 0
done
cmp "$OUT/original-fault.status" "$OUT/rewritten-fault.status"
cmp "$OUT/original-fault.stdout" "$OUT/rewritten-fault.stdout"
cmp "$OUT/original-fault.stderr" "$OUT/rewritten-fault.stderr"
grep '^[(]RuleSteps ' "$OUT/controls.stdout"
printf '18 source-admission/iteration controls, %s execution/effect/choice/quotation checks and one fault check passed\n' "${#queries[@]}"
