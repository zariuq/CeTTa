#!/usr/bin/env bash

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${CETTA_BIN:-$ROOT/runtime/cetta-core-runtime-stats}"
BUILD_CFG="${CETTA_BUILD_CONFIG:-$ROOT/runtime/bootstrap/build_config.core.runtime-stats.h}"
RUNTIME_DIR="$ROOT/runtime"
BENCH_DIR="$RUNTIME_DIR/bench_answer_ref_demand"
SEED_COUNT="${1:-32}"
VM_LIMIT_KIB="${VM_LIMIT_KIB:-6291456}"

if [ ! -x "$BIN" ]; then
    echo "error: missing executable $BIN" >&2
    echo "hint: run 'make BUILD=core ENABLE_RUNTIME_STATS=1 runtime/cetta-core-runtime-stats' first or set CETTA_BIN" >&2
    exit 1
fi

if [ ! -f "$BUILD_CFG" ] || \
   ! grep -Fq '#define CETTA_BUILD_WITH_RUNTIME_STATS 1' "$BUILD_CFG"; then
    echo "error: benchmark requires CETTA_BUILD_WITH_RUNTIME_STATS=1" >&2
    echo "hint: set CETTA_BUILD_CONFIG to the matching runtime-stats build_config header" >&2
    exit 1
fi

mkdir -p "$RUNTIME_DIR" "$BENCH_DIR"

program_path() {
    local scenario="$1"
    printf '%s/%s_%s.metta\n' "$BENCH_DIR" "$scenario" "$SEED_COUNT"
}

emit_multi_expected() {
    local i
    local j
    for ((i = 0; i < SEED_COUNT; i++)); do
        for ((j = 0; j < SEED_COUNT; j++)); do
            printf '           (Pair (Seed %d $a $b) (Seed %d $c $d))\n' "$i" "$j"
        done
    done
}

prepare_program() {
    local scenario="$1"
    local path
    local tmp

    path="$(program_path "$scenario")"
    tmp="$(mktemp "$BENCH_DIR/${scenario}_${SEED_COUNT}.XXXXXX")"
    {
        if [ "$scenario" = "multi" ]; then
            printf '; Warm multi-answer delayed replay benchmark.\n'
            printf '(= (project2 $x $y)\n'
            printf '   (Pair $x $y))\n\n'
            printf '(= (pair-via-eq $ss)\n'
            printf '   (match $ss (: $name $body)\n'
            printf '     (match $ss (: $other $body2)\n'
            printf '       (project2 $body $body2))))\n\n'
            printf '!(bind! &ss (new-space))\n'
            for ((i = 0; i < SEED_COUNT; i++)); do
                printf '!(add-atom &ss (: ax-%d (Seed %d $x $y)))\n' "$i" "$i"
            done
            printf '\n'
            printf '!(let* (\n'
            printf '    (() (pragma! search-table-mode variant))\n'
            printf '    (() (assertAlphaEqualToResult\n'
            printf '          (pair-via-eq &ss)\n'
            printf '          (\n'
            emit_multi_expected
            printf '          )))\n'
            printf '    (() (reset-runtime-stats!))\n'
            printf '    (() (assertAlphaEqualToResult\n'
            printf '          (pair-via-eq &ss)\n'
            printf '          (\n'
            emit_multi_expected
            printf '          )))\n'
            printf '  )\n'
            printf '  ())\n'
        else
            printf '; Warm single-tail delayed replay benchmark.\n'
            printf '(= (project1 $x)\n'
            printf '   (Pair $x))\n\n'
            printf '(= (single-via-eq $ss)\n'
            printf '   (match $ss (: only $body)\n'
            printf '     (project1 $body)))\n\n'
            printf '!(bind! &ss (new-space))\n'
            printf '!(add-atom &ss (: only (Seed only $x $y)))\n'
            for ((i = 0; i < SEED_COUNT; i++)); do
                printf '!(add-atom &ss (: filler-%d (Noise %d)))\n' "$i" "$i"
            done
            printf '\n'
            printf '!(let* (\n'
            printf '    (() (pragma! search-table-mode variant))\n'
            printf '    (() (assertAlphaEqualToResult\n'
            printf '          (single-via-eq &ss)\n'
            printf '          ((Pair (Seed only $a $b)))))\n'
            printf '    (() (reset-runtime-stats!))\n'
            printf '    (() (assertAlphaEqualToResult\n'
            printf '          (single-via-eq &ss)\n'
            printf '          ((Pair (Seed only $a $b)))))\n'
            printf '  )\n'
            printf '  ())\n'
        fi
    } > "$tmp"
    mv "$tmp" "$path"
    printf '%s\n' "$path"
}

counter_value() {
    local file="$1"
    local key="$2"
    awk -v want="$key" '
        $1 == "runtime-counter" && $2 == want { print $3; found = 1; exit }
        END { if (!found) exit 1 }
    ' "$file"
}

ratio_or_inf() {
    local numerator="$1"
    local denominator="$2"
    awk -v n="$numerator" -v d="$denominator" 'BEGIN {
        if (d == 0) {
            print "inf";
        } else {
            printf "%.2f", n / d;
        }
    }'
}

run_case() {
    local scenario="$1"
    local expected_answers="$2"
    local program
    local out_file
    local log_file
    local table_hit
    local table_miss
    local ref_emit
    local inflate_calls
    local materialize_calls
    local materialize_bytes
    local promoted_count
    local promoted_bytes
    local elapsed
    local rss_kb

    program="$(prepare_program "$scenario")"
    out_file="$BENCH_DIR/${scenario}_${SEED_COUNT}.out"
    log_file="$BENCH_DIR/${scenario}_${SEED_COUNT}.log"

    /usr/bin/time -f 'ELAPSED=%e\nRSS_KB=%M' \
        bash -lc "ulimit -v '$VM_LIMIT_KIB' && cd '$ROOT' && '$BIN' --emit-runtime-stats --quiet '$program'" \
        > "$out_file" 2> "$log_file"

    table_hit="$(counter_value "$log_file" "table-hit")"
    table_miss="$(counter_value "$log_file" "table-miss")"
    ref_emit="$(counter_value "$log_file" "answer-ref-emit")"
    inflate_calls="$(counter_value "$log_file" "answer-ref-inflate-call")"
    materialize_calls="$(counter_value "$log_file" "answer-ref-materialize-call")"
    materialize_bytes="$(counter_value "$log_file" "answer-ref-materialize-bytes")"
    promoted_count="$(counter_value "$log_file" "query-episode-promoted-answer-count")"
    promoted_bytes="$(counter_value "$log_file" "query-episode-promoted-answer-bytes")"
    elapsed="$(awk -F= '/^ELAPSED=/{print $2}' "$log_file")"
    rss_kb="$(awk -F= '/^RSS_KB=/{print $2}' "$log_file")"

    if [ "$table_hit" -le 0 ]; then
        echo "error: expected warm table-hit for $scenario, got $table_hit" >&2
        tail -n 40 "$log_file" >&2
        exit 1
    fi

    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$scenario" "$SEED_COUNT" "$expected_answers" "$table_hit" "$table_miss" \
        "$ref_emit" "$inflate_calls" "$materialize_calls" "$materialize_bytes" \
        "$promoted_count" "$promoted_bytes" "$elapsed/$rss_kb"
}

printf '%-8s %8s %10s %10s %10s %10s %10s %10s %12s %10s %12s %14s\n' \
    "scenario" "seeds" "answers" "table-hit" "table-miss" "ref-emit" "inflate" \
    "mat-calls" "mat-bytes" "promoted" "prom-bytes" "wall/rss"
printf '%-8s %8s %10s %10s %10s %10s %10s %10s %12s %10s %12s %14s\n' \
    "--------" "--------" "----------" "----------" "----------" "----------" \
    "----------" "----------" "------------" "----------" "------------" "--------------"

multi_row="$(run_case multi "$((SEED_COUNT * SEED_COUNT))")"
single_row="$(run_case single 1)"

printf '%s\n' "$multi_row" | awk -F '\t' '{ printf "%-8s %8s %10s %10s %10s %10s %10s %10s %12s %10s %12s %14s\n", $1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12 }'
printf '%s\n' "$single_row" | awk -F '\t' '{ printf "%-8s %8s %10s %10s %10s %10s %10s %10s %12s %10s %12s %14s\n", $1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12 }'

multi_mat_calls="$(printf '%s\n' "$multi_row" | awk -F '\t' '{ print $8 }')"
multi_mat_bytes="$(printf '%s\n' "$multi_row" | awk -F '\t' '{ print $9 }')"
multi_promoted="$(printf '%s\n' "$multi_row" | awk -F '\t' '{ print $10 }')"
multi_promoted_bytes="$(printf '%s\n' "$multi_row" | awk -F '\t' '{ print $11 }')"
single_mat_calls="$(printf '%s\n' "$single_row" | awk -F '\t' '{ print $8 }')"
single_mat_bytes="$(printf '%s\n' "$single_row" | awk -F '\t' '{ print $9 }')"
single_promoted="$(printf '%s\n' "$single_row" | awk -F '\t' '{ print $10 }')"
single_promoted_bytes="$(printf '%s\n' "$single_row" | awk -F '\t' '{ print $11 }')"

printf '\n'
printf 'ratio materialize_calls multi/single=%s\n' \
    "$(ratio_or_inf "$multi_mat_calls" "$single_mat_calls")"
printf 'ratio materialize_bytes multi/single=%s\n' \
    "$(ratio_or_inf "$multi_mat_bytes" "$single_mat_bytes")"
printf 'ratio promoted_answers multi/single=%s\n' \
    "$(ratio_or_inf "$multi_promoted" "$single_promoted")"
printf 'ratio promoted_bytes multi/single=%s\n' \
    "$(ratio_or_inf "$multi_promoted_bytes" "$single_promoted_bytes")"
printf 'note current warm witnesses exercise replay materialization; answer-ref emit/inflate may remain zero on this branch.\n'
