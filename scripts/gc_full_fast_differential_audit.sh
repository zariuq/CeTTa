#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${CETTA_BIN:-$ROOT/cetta}"
MANIFEST="${CETTA_GC_DIFF_MANIFEST:-$ROOT/tests/test_manifest.tsv}"
BUDGET_MB="${CETTA_GC_DIFF_BUDGET_MB:-1}"
GC_OFF_TIMEOUT="${CETTA_GC_DIFF_GC_OFF_TIMEOUT:-25}"
GC_ON_TIMEOUT="${CETTA_GC_DIFF_GC_ON_TIMEOUT:-120}"

if [ ! -x "$BIN" ]; then
    echo "error: missing executable $BIN" >&2
    exit 2
fi

if [ ! -f "$MANIFEST" ]; then
    echo "error: missing manifest $MANIFEST" >&2
    exit 2
fi

normalize() {
    sed -E 's/0x[0-9a-fA-F]+/0xADDR/g'
}

is_semantic_fast_lane() {
    case "$1" in
        test|test-profiles|test-python|test-eval-gc-adversarial)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

is_supported_build() {
    case "$1" in
        main|python)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

is_semantic_oracle() {
    case "$1" in
        golden|property)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

run_case() {
    local gc="$1"
    local timeout_sec="$2"
    local profile="$3"
    local test_path="$4"
    shift 4

    local args=("$BIN")
    if [ -n "$profile" ]; then
        args+=(--profile "$profile")
    fi
    args+=(--lang he "$ROOT/$test_path")

    timeout "$timeout_sec" \
        env CETTA_GC="$gc" CETTA_GC_BUDGET_MB="$BUDGET_MB" "$@" "${args[@]}" 2>&1 | normalize
}

pass=0
fail=0
seen=0

while IFS=$'\t' read -r path lang syntax profile build space_engine lane expect notes; do
    if [ "$path" = "path" ]; then
        continue
    fi

    if [ "$lang" != "he" ] || [ "$syntax" != "metta" ]; then
        continue
    fi
    if ! is_semantic_fast_lane "$lane"; then
        continue
    fi
    if ! is_supported_build "$build"; then
        continue
    fi
    if ! is_semantic_oracle "$expect"; then
        continue
    fi
    if [ "$space_engine" != "native" ]; then
        continue
    fi
    if [ ! -f "$ROOT/$path" ]; then
        echo "FAIL: $path (manifest path missing)" >&2
        fail=$((fail + 1))
        continue
    fi

    seen=$((seen + 1))

    set +e
    gc_off="$(run_case 0 "$GC_OFF_TIMEOUT" "$profile" "$path")"
    gc_off_status=$?
    set -e

    if [ "$gc_off_status" -eq 124 ]; then
        echo "FAIL: $path (GC-off timeout after ${GC_OFF_TIMEOUT}s)" >&2
        fail=$((fail + 1))
        continue
    fi
    if [ "$gc_off_status" -ne 0 ]; then
        echo "FAIL: $path (GC-off exit $gc_off_status)" >&2
        printf '%s\n' "$gc_off" | head -80 >&2
        fail=$((fail + 1))
        continue
    fi

    set +e
    gc_on="$(run_case 1 "$GC_ON_TIMEOUT" "$profile" "$path")"
    gc_on_status=$?
    set -e

    if [ "$gc_on_status" -eq 124 ]; then
        echo "FAIL: $path (GC-on timeout after ${GC_ON_TIMEOUT}s)" >&2
        fail=$((fail + 1))
        continue
    fi
    if [ "$gc_on_status" -ne 0 ]; then
        echo "FAIL: $path (GC-on exit $gc_on_status)" >&2
        printf '%s\n' "$gc_on" | head -80 >&2
        fail=$((fail + 1))
        continue
    fi

    if [ "$gc_on" = "$gc_off" ]; then
        echo "PASS: $path"
        pass=$((pass + 1))
    else
        echo "FAIL: $path (GC-on differs from GC-off)" >&2
        diff <(printf '%s\n' "$gc_off") <(printf '%s\n' "$gc_on") | head -80 >&2
        fail=$((fail + 1))
    fi
done < "$MANIFEST"

echo "---"
echo "$pass passed, $fail failed, $seen considered"
if [ "$seen" -eq 0 ]; then
    echo "FAIL: GC differential selected no semantic cases" >&2
    exit 1
fi
[ "$fail" -eq 0 ] && [ "$pass" -eq "$seen" ]
