#!/bin/sh
# The lambda-calculus client of rulec's generic components (Lambda.module): the module compiled with
# every rule as written (no binding table) and compiled through the components (lambda.bindings)
# give expected.txt on every case of cases.txt, and in check mode every component call and kept
# result equals its rules.  CeTTa's own reader prints the module's rows.
#   sh tests/rulec/lambda/gate.sh [CETTA-BINARY]        (from the CeTTa root)
set -eu
L=$(cd "$(dirname "$0")" && pwd); ROOT=$(cd "$L/../../.." && pwd)
CETTA=${1:-$ROOT/cetta}
W=$ROOT/runtime/rulec-lambda
rm -rf "$W"; mkdir -p "$W"
CC=${CC:-cc}
$CC -O2 -o "$W/rulec" "$ROOT"/tools/rulec/compiler/*.c
$CC -O2 -o "$W/rulec_validate" "$ROOT/tools/rulec/rulec_validate.c"
printf '!(import! &self "%s")\n!(wire:dump "%s" "Calc")\n' "$ROOT/tools/rulec/dump_wire.metta" "$L/Lambda.module" > "$W/dump.metta"
"$CETTA" "$W/dump.metta" | grep '^(' > "$W/Lambda.wire"
build() {   # build OUT [BINDINGS] [DEFS]
  mkdir -p "$W/$1"
  "$W/rulec" "$W/Lambda.wire" --dag "$W/$1/Lambda.dag" --cache-constants ${2:+--bindings "$2"} \
    --emit "$W/$1/lambda.c" --emit-h "$W/$1/lambda_ids.h" 2> "$W/$1/rulec.err"
  "$W/rulec_validate" "$W/Lambda.wire" "$W/$1/Lambda.dag" "$W/$1/lambda.c" > "$W/$1/validate.out"
  $CC -O2 -std=gnu11 ${3:-} -I"$ROOT/native/rule_runtime" -I"$ROOT/native/rule_components" -I"$W/$1" \
    "$W/$1/lambda.c" "$L/lam.c" "$ROOT/native/rule_runtime/rc_rt.c" "$ROOT"/native/rule_components/*.c -o "$W/$1/lam"
}
build rules
build components "$L/lambda.bindings"
build check "$L/lambda.bindings" -DRC_CHECK_NATIVE
fail=0
for v in rules components; do
  "$W/$v/lam" "$L/cases.txt" > "$W/$v.out"
  if cmp -s "$W/$v.out" "$L/expected.txt"; then echo "$v: $(grep -c '=>' "$W/$v.out") cases as expected"; else echo "$v: differs from expected"; fail=1; fi
done
"$W/check/lam" --check "$L/cases.txt" > "$W/check.out" || fail=1
tail -1 "$W/check.out"
rm -rf "$W"
exit $fail
