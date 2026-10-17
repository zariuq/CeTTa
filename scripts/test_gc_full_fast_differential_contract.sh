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

# Empty default profiles are valid. Selection and the final row must survive
# both adjacent delimiters and a file without a terminal newline.
printf '%s\n' \
    $'path\tlang\tsyntax\tprofile\tbuild\tspace_engine\tlane\texpect\tnotes' \
    $'tests/test_arithmetic.metta\the\tmetta\textended\tmain\tnative\ttest\tgolden\textended fixture' \
    >"$manifest"
printf '%s' \
    $'tests/test_arithmetic.metta\the\tmetta\t\tmain\tnative\ttest\tgolden\t' \
    >>"$manifest"
env "${common_env[@]}" GC_DIFFERENTIAL_FIXTURE_MODE=pass \
    "$AUDIT" >"$scratch/empty-profile.log" 2>&1
grep -Fq '2 passed, 0 failed, 2 considered' "$scratch/empty-profile.log"

printf '%s\n' $'tests/test_arithmetic.metta\the\tmetta\t' >"$manifest"
if env "${common_env[@]}" GC_DIFFERENTIAL_FIXTURE_MODE=pass \
        "$AUDIT" >"$scratch/malformed.log" 2>&1; then
    echo "FAIL: GC differential accepted an incomplete manifest" >&2
    exit 1
fi
grep -Fq 'FAIL: malformed GC differential manifest row' "$scratch/malformed.log"

echo "PASS: GC differential preserves empty profiles and final rows, and rejects timeouts and incomplete manifests"
