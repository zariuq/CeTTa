#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CETTA_BIN="${CETTA_BIN:-$ROOT/cetta}"
MANIFEST="$ROOT/benchmarks/mam_jetta/manifest.tsv"
SCALE="${MAM_JETTA_SCALE:-smoke}"
LANGUAGES="${MAM_JETTA_LANGUAGES:-he prime}"
CASES="${MAM_JETTA_CASES:-}"
TIMEOUT_SECONDS="${MAM_JETTA_TIMEOUT:-120}"
REQUIRE_PASS="${MAM_JETTA_REQUIRE_PASS:-0}"

if [[ ! -x "$CETTA_BIN" ]]; then
  echo "CeTTa binary not found or not executable: $CETTA_BIN" >&2
  exit 1
fi

if [[ "$REQUIRE_PASS" != "0" && "$REQUIRE_PASS" != "1" ]]; then
  echo "MAM_JETTA_REQUIRE_PASS must be 0 or 1" >&2
  exit 1
fi

case "$SCALE" in
  smoke|curve|paper) ;;
  *)
    echo "MAM_JETTA_SCALE must be smoke, curve, or paper" >&2
    exit 1
    ;;
esac

stamp="$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$ROOT/runtime/bench_mam_jetta"
run_dir="$(mktemp -d "$ROOT/runtime/bench_mam_jetta/${stamp}.XXXXXX")"
results="$run_dir/results.tsv"
printf 'language\tcase\tsize\texpected\tactual\twall_seconds\trss_kb\tstatus\tlog\n' >"$results"

wanted_case() {
  local candidate="$1"
  [[ -z "$CASES" ]] && return 0
  local selected
  for selected in $CASES; do
    [[ "$selected" == "$candidate" ]] && return 0
  done
  return 1
}

sizes_for() {
  local case_name="$1"
  local paper_scale="$2"
  case "$SCALE:$case_name" in
    smoke:fibonacci) echo "10" ;;
    smoke:interpreter|smoke:differentiation) echo "1" ;;
    smoke:synthesis|smoke:backward_chaining) echo "1" ;;
    curve:fibonacci) echo "10 15 20 25 30" ;;
    curve:interpreter|curve:differentiation) echo "1 10 100 1000" ;;
    curve:synthesis) echo "1 2 5 10 20 40" ;;
    curve:backward_chaining) echo "1 10 100 1000" ;;
    paper:*) echo "$paper_scale" ;;
  esac
}

fib_expected() {
  local n="$1"
  local a=0
  local b=1
  local i
  for ((i = 0; i < n; i++)); do
    local next=$((a + b))
    a="$b"
    b="$next"
  done
  echo "$a"
}

expected_for() {
  local checksum="$1"
  local n="$2"
  case "$checksum" in
    fib) fib_expected "$n" ;;
    identity) echo "$n" ;;
    mul:*) echo "$((n * ${checksum#mul:}))" ;;
    *)
      echo "unknown checksum rule: $checksum" >&2
      return 1
      ;;
  esac
}

run_one() {
  local language="$1"
  local case_name="$2"
  local template="$3"
  local n="$4"
  local checksum="$5"
  local expected
  expected="$(expected_for "$checksum" "$n")"

  local source="$run_dir/${language}_${case_name}_${n}.metta"
  local log="$run_dir/${language}_${case_name}_${n}.log"
  local log_relative="${log#"$ROOT/"}"
  local timing="$run_dir/${language}_${case_name}_${n}.time"
  sed "s/@N@/$n/g" "$ROOT/benchmarks/mam_jetta/$template" >"$source"

  local rc=0
  set +e
  timeout "$TIMEOUT_SECONDS" \
    /usr/bin/time -f '%e\t%M' -o "$timing" \
    "$CETTA_BIN" --lang "$language" "$source" >"$log" 2>&1
  rc=$?
  set -e

  local wall="NA"
  local rss="NA"
  if [[ -s "$timing" ]]; then
    IFS=$'\t' read -r wall rss <"$timing" || true
  fi

  local status="ok"
  if [[ "$rc" -eq 124 ]]; then
    status="timeout"
  elif [[ "$rc" -ne 0 ]]; then
    status="exit:$rc"
  fi

  local actual="NA"
  if [[ "$status" == "ok" ]]; then
    actual="$(
      sed -n 's/^\[\(-\{0,1\}[0-9][0-9]*\)\]$/\1/p' "$log" |
        tail -n 1
    )"
    if [[ "$actual" != "$expected" ]]; then
      status="wrong-answer"
    fi
    if rg -q \
      -e '\(Error' \
      -e 'Stack overflow' \
      -e 'AddressSanitizer' \
      -e 'runtime error:' \
      "$log"; then
      status="runtime-error"
    fi
  fi

  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$language" "$case_name" "$n" "$expected" "${actual:-NA}" \
    "$wall" "$rss" "$status" "$log_relative" >>"$results"
}

while IFS=$'\t' read -r case_name template paper_scale checksum; do
  [[ "$case_name" == "case" ]] && continue
  wanted_case "$case_name" || continue
  for language in $LANGUAGES; do
    for n in $(sizes_for "$case_name" "$paper_scale"); do
      run_one "$language" "$case_name" "$template" "$n" "$checksum"
    done
  done
done <"$MANIFEST"

cat "$results"

if awk -F '\t' 'NR > 1 && $8 != "ok" { failed = 1 } END { exit failed }' "$results"; then
  echo "MAMJeTTaSuiteSummary PASS $SCALE"
else
  echo "MAMJeTTaSuiteSummary FAIL $SCALE" >&2
  if [[ "$REQUIRE_PASS" == "1" ]]; then
    exit 1
  fi
fi
