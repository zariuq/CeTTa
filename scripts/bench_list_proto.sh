#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "$0")/.." && pwd)
CETTA_BIN="${CETTA_BIN:-$ROOT/cetta}"
TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

SIZES=("$@")
if [[ ${#SIZES[@]} -eq 0 ]]; then
    SIZES=(100 500)
fi

KERNELS=(build-sum reverse-sum append-sum map-sum)
MODES=(expr cons)

expected_value() {
    local kernel=$1
    local n=$2
    case "$kernel" in
        build-sum|reverse-sum)
            echo $(( n * (n - 1) / 2 ))
            ;;
        append-sum)
            echo $(( n * (n - 1) ))
            ;;
        map-sum)
            echo $(( n * (n + 1) / 2 ))
            ;;
        *)
            echo "unknown kernel: $kernel" >&2
            return 1
            ;;
    esac
}

label_for() {
    local kernel=$1
    case "$kernel" in
        build-sum) echo "AWFY-style build/traverse" ;;
        reverse-sum) echo "Scheme/Haskell reverse" ;;
        append-sum) echo "nofib-style append" ;;
        map-sum) echo "Sandmark-style pipeline" ;;
        *) echo "$kernel" ;;
    esac
}

run_case() {
    local mode=$1
    local kernel=$2
    local n=$3
    local fn="bench:${mode}-${kernel}"
    local expected
    expected=$(expected_value "$kernel" "$n")

    local program="$TMPDIR/${mode}_${kernel}_${n}.metta"
    cat > "$program" <<PROG
!(import! &self list_bench_proto)
!($fn $n)
PROG

    local out="$TMPDIR/out.txt"
    local timing="$TMPDIR/timing.txt"
    local status=0
    /usr/bin/time -f 'elapsed=%e rss=%MKB' \
        bash -lc "ulimit -s unlimited && '$CETTA_BIN' '$program'" \
        >"$out" 2>"$timing" || status=$?

    local result
    result=$(grep -oE -- '-?[0-9]+' "$out" | tail -1 || true)
    if [[ $status -ne 0 || -z "$result" || "$result" != "$expected" ]]; then
        echo "FAIL mode=$mode kernel=$kernel n=$n expected=$expected got=${result:-<none>} status=$status" >&2
        echo "--- output ---" >&2
        cat "$out" >&2 || true
        echo "--- timing ---" >&2
        cat "$timing" >&2 || true
        return 1
    fi

    printf '%-6s  %-22s  n=%-6s  result=%-10s  %s\n' \
        "$mode" "$(label_for "$kernel")" "$n" "$result" "$(cat "$timing")"
}

if [[ ! -x "$CETTA_BIN" ]]; then
    echo "error: $CETTA_BIN is missing or not executable" >&2
    echo "hint: run 'make' in $ROOT first" >&2
    exit 1
fi

echo "== CeTTa list prototype benchmark =="
echo "binary: $CETTA_BIN"
for n in "${SIZES[@]}"; do
    echo
    echo "-- size $n --"
    for kernel in "${KERNELS[@]}"; do
        for mode in "${MODES[@]}"; do
            run_case "$mode" "$kernel" "$n"
        done
    done
done
