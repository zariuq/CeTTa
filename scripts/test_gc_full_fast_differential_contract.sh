#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AUDIT="$ROOT/scripts/gc_full_fast_differential_audit.sh"

mkdir -p "$ROOT/runtime"
scratch="$(mktemp -d "$ROOT/runtime/gc-differential-contract.XXXXXX")"
trap 'rm -rf "$scratch"' EXIT INT TERM

fake_bin="$scratch/fake-cetta"
manifest="$scratch/manifest.tsv"

printf '%s\n' \
    '#!/usr/bin/env bash' \
    'set -euo pipefail' \
    'case "${GC_DIFFERENTIAL_FIXTURE_MODE:-pass}:${CETTA_GC:-}" in' \
    '    gc-off-timeout:0|gc-on-timeout:1) sleep 5 ;;' \
    'esac' \
    'printf "[same-observation]\\n"' >"$fake_bin"
chmod +x "$fake_bin"

printf '%s\n' \
    $'path\tlang\tsyntax\tprofile\tbuild\tspace_engine\tlane\texpect\tnotes' \
    $'tests/test_arithmetic.metta\the\tmetta\textended\tmain\tnative\ttest\tgolden\tcontract fixture' \
    >"$manifest"

common_env=(
    CETTA_BIN="$fake_bin"
    CETTA_GC_DIFF_MANIFEST="$manifest"
    CETTA_GC_DIFF_GC_OFF_TIMEOUT=0.05
    CETTA_GC_DIFF_GC_ON_TIMEOUT=0.05
)

env "${common_env[@]}" GC_DIFFERENTIAL_FIXTURE_MODE=pass \
    "$AUDIT" >"$scratch/pass.log" 2>&1
grep -Fq '1 passed, 0 failed, 1 considered' "$scratch/pass.log"

if env "${common_env[@]}" GC_DIFFERENTIAL_FIXTURE_MODE=gc-off-timeout \
        "$AUDIT" >"$scratch/gc-off-timeout.log" 2>&1; then
    echo "FAIL: GC differential accepted a GC-off timeout" >&2
    exit 1
fi
grep -Fq \
    'FAIL: tests/test_arithmetic.metta (GC-off timeout after 0.05s)' \
    "$scratch/gc-off-timeout.log"

if env "${common_env[@]}" GC_DIFFERENTIAL_FIXTURE_MODE=gc-on-timeout \
        "$AUDIT" >"$scratch/gc-on-timeout.log" 2>&1; then
    echo "FAIL: GC differential accepted a GC-on timeout" >&2
    exit 1
fi
grep -Fq \
    'FAIL: tests/test_arithmetic.metta (GC-on timeout after 0.05s)' \
    "$scratch/gc-on-timeout.log"

echo "PASS: GC differential accepts exact agreement and rejects both timeout directions"
