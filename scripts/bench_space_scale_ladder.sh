#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BACKEND_SCRIPT="${CETTA_BENCH_BACKEND_SCRIPT:-$ROOT/scripts/bench_space_backend_matrix.sh}"
TRANSFER_SCRIPT="${CETTA_BENCH_TRANSFER_SCRIPT:-$ROOT/scripts/bench_space_transfer_matrix.sh}"
PROPERTY_MANIFEST="$ROOT/benchmarks/main_readiness_properties.json"
SIZES_STR="${1:-1000000 10000000}"
MATCH_ROUNDS="${2:-1}"
CASE_TIMEOUT_S="${CETTA_SCALE_CASE_TIMEOUT_S:-140}"
GENERATOR_FACT_COUNT="${CETTA_GENERATOR_LITMUS_FACT_COUNT:-1000000}"

# Observational calibration, not a portable CeTTa expectation.  Recorded on
# 2026-08-12 on oruzi-home-MS-7E56 (AMD Ryzen 7 9800X3D, Linux x86_64), with
# result printing suppressed: PeTTa 43705f5d9ff8958ffe7f0aa6777fb8477f2401f2,
# with local run.sh changes limited to quoting and Python-library discovery,
# took 0.90 s for one million streamed facts and three scans.  CeTTa
# fd85864f48d9284657f23bedbf61101a285f36fd took 3.20 s for its deliberately
# materialized one-million-element range.  Re-run the same harness on another
# machine before interpreting this wall-clock reference there.
GENERATOR_REFERENCE_SECONDS=0.90
GENERATOR_REFERENCE_MULTIPLIER=5
GENERATOR_REFERENCE_CEILING=$(awk -v seconds="$GENERATOR_REFERENCE_SECONDS" \
    -v multiplier="$GENERATOR_REFERENCE_MULTIPLIER" \
    'BEGIN { printf "%.2f", seconds * multiplier }')
GENERATOR_TIMEOUT_S="${CETTA_GENERATOR_LITMUS_TIMEOUT_S:-$GENERATOR_REFERENCE_CEILING}"
GENERATOR_ADMISSION=observed
MAX_TIME_EXPONENT=1.50
MAX_RSS_EXPONENT=1.25
RUNTIME_DIR="${CETTA_SCALE_RUNTIME_DIR:-$ROOT/runtime/bench_space_scale_ladder}"

# This is the complete qualification surface.  Do not make it environment
# configurable: narrowing a required case list must not turn absence into a
# passing main-readiness result.  mork-load-act is the live MORK backend after
# ACT population; mork-open-act remains separate because it uses the opened ACT
# representation directly.
BACKEND_MODES_STR="native pathmap mork-open-act mork-load-act"
TRANSFER_CASES_STR="native-to-pathmap native-to-mork-live pathmap-to-native pathmap-to-mork-live mork-live-to-native mork-live-to-pathmap mork-live-to-open-act mork-live-to-load-act"

failure_count=0
timeout_count=0
observed_timeout_count=0
generator_litmus_status=not-run

die() {
    printf '%s\n' "$*" >&2
    exit 1
}

is_positive_integer() {
    [[ "$1" =~ ^[0-9]+$ ]] && [ "$1" -gt 0 ]
}

is_positive_number() {
    awk -v value="$1" 'BEGIN { exit !(value + 0 > 0) }'
}

is_positive_integer "$MATCH_ROUNDS" || die "MATCH_ROUNDS must be a positive integer"
is_positive_number "$CASE_TIMEOUT_S" || die "CETTA_SCALE_CASE_TIMEOUT_S must be positive"
is_positive_integer "$GENERATOR_FACT_COUNT" || die "CETTA_GENERATOR_LITMUS_FACT_COUNT must be a positive integer"
is_positive_number "$GENERATOR_TIMEOUT_S" || die "CETTA_GENERATOR_LITMUS_TIMEOUT_S must be positive"
[ -x "$BACKEND_SCRIPT" ] || die "Missing $BACKEND_SCRIPT"
[ -x "$TRANSFER_SCRIPT" ] || die "Missing $TRANSFER_SCRIPT"
[ -f "$PROPERTY_MANIFEST" ] || die "Missing $PROPERTY_MANIFEST"

IFS=' ' read -r -a sizes <<< "$SIZES_STR"
[ "${#sizes[@]}" -eq 2 ] || die "Exactly two ascending sizes are required"
base_size=${sizes[0]}
frontier_size=${sizes[1]}
is_positive_integer "$base_size" || die "Invalid base size: $base_size"
is_positive_integer "$frontier_size" || die "Invalid frontier size: $frontier_size"
[ "$frontier_size" -gt "$base_size" ] || die "Sizes must be strictly ascending"

IFS=' ' read -r -a backend_modes <<< "$BACKEND_MODES_STR"
IFS=' ' read -r -a transfer_cases <<< "$TRANSFER_CASES_STR"
expected_case_count=$((${#backend_modes[@]} + ${#transfer_cases[@]}))

mkdir -p "$RUNTIME_DIR"
corpus_dir=$(mktemp -d "$RUNTIME_DIR/.corpora.XXXXXX")
cleanup() {
    find "$corpus_dir" -mindepth 1 -maxdepth 1 -type f -delete
    rmdir "$corpus_dir"
}
trap cleanup EXIT

declare -A corpus_paths=()
declare -A act_paths=()
declare -A base_ns=()
declare -A base_rss=()
realized_witnesses="$corpus_dir/realized-frontier-witnesses.txt"
: >"$realized_witnesses"
base_case_count=0
frontier_case_count=0

needs_act_corpus() {
    local mode
    for mode in "${backend_modes[@]}"; do
        case "$mode" in
            mork-live|mork-open-act|mork-load-act) return 0 ;;
        esac
    done
    for mode in "${transfer_cases[@]}"; do
        case "$mode" in
            *mork*) return 0 ;;
        esac
    done
    return 1
}

record_measurement() {
    local suite_kind=$1
    local case_id=$2
    local fact_count=$3
    shift 3
    printf 'measurement\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$suite_kind" "$case_id" "$fact_count" "$fact_count" "$@"
}

validate_route_counters() {
    local suite_kind=$1
    local case_id=$2
    local fact_count=$3
    local pathmap_stores=$4
    local mork_adds=$5
    local mork_batch_items=$6
    local expected_pathmap=0
    local expected_mork_adds=0
    local expected_mork_batch=0

    case "$suite_kind:$case_id" in
        backend:native|backend:mork-open-act|backend:mork-load-act|transfer:mork-live-to-native|transfer:mork-live-to-open-act|transfer:mork-live-to-load-act)
            ;;
        backend:pathmap|transfer:native-to-pathmap|transfer:pathmap-to-native|transfer:pathmap-to-mork-live|transfer:mork-live-to-pathmap)
            expected_pathmap=$fact_count
            ;;
        transfer:native-to-mork-live)
            expected_mork_batch=$fact_count
            ;;
        *)
            printf 'No frontier route contract for %s:%s\n' \
                "$suite_kind" "$case_id" >&2
            return 1
            ;;
    esac

    if [ "$pathmap_stores" != "$expected_pathmap" ] || \
       [ "$mork_adds" != "$expected_mork_adds" ] || \
       [ "$mork_batch_items" != "$expected_mork_batch" ]; then
        printf 'Frontier route mismatch for %s:%s/%s: got (%s,%s,%s), expected (%s,%s,%s)\n' \
            "$suite_kind" "$case_id" "$fact_count" \
            "$pathmap_stores" "$mork_adds" "$mork_batch_items" \
            "$expected_pathmap" "$expected_mork_adds" "$expected_mork_batch" >&2
        return 1
    fi
}

print_summary() {
    if [ "$failure_count" -eq 0 ]; then
        printf 'SPACE_FRONTIER_STATUS=passed\n'
    else
        printf 'SPACE_FRONTIER_STATUS=failed\n'
    fi
    printf 'SPACE_FRONTIER_FAILURES=%s\n' "$failure_count"
    printf 'SPACE_FRONTIER_TIMEOUTS=%s\n' "$timeout_count"
    printf 'SPACE_FRONTIER_OBSERVED_TIMEOUTS=%s\n' "$observed_timeout_count"
    printf 'SPACE_FRONTIER_BASE_SIZE=%s\n' "$base_size"
    printf 'SPACE_FRONTIER_SIZE=%s\n' "$frontier_size"
    printf 'SPACE_FRONTIER_REQUIRED_CASES=%s\n' "$expected_case_count"
    printf 'SPACE_FRONTIER_BASE_CASES=%s\n' "$base_case_count"
    printf 'SPACE_FRONTIER_EXECUTED_CASES=%s\n' "$frontier_case_count"
    printf 'GENERATOR_LITMUS_STATUS=%s\n' "$generator_litmus_status"
    printf 'GENERATOR_CETTA_EXPECTATION=observational-capability\n'
    printf 'GENERATOR_LITMUS_AUTHORITATIVE_READINESS=0\n'
    printf 'GENERATOR_REFERENCE_ENGINE=petta\n'
    printf 'GENERATOR_REFERENCE_SECONDS=%s\n' "$GENERATOR_REFERENCE_SECONDS"
    printf 'GENERATOR_REFERENCE_MULTIPLIER=%s\n' "$GENERATOR_REFERENCE_MULTIPLIER"
    printf 'GENERATOR_REFERENCE_CEILING_SECONDS=%s\n' "$GENERATOR_REFERENCE_CEILING"
}

run_generator_litmus() {
    local log="$RUNTIME_DIR/generator_materialized_${GENERATOR_FACT_COUNT}.log"
    local shell_status=0
    local row=""
    local time_s=unknown
    local rss_kb=unknown
    local scenario_ns=unknown

    (
        cd "$ROOT"
        /usr/bin/time -f 'outer_elapsed_sec=%e outer_rss_kb=%M' \
            timeout --kill-after=5 "$GENERATOR_TIMEOUT_S" \
            env -u CETTA_BENCH_CORPUS_PATH -u CETTA_BENCH_ACT_PATH \
                CETTA_BENCH_MATERIALIZED_RANGE=1 \
                "$BACKEND_SCRIPT" native "$GENERATOR_FACT_COUNT" 1 insert_only
    ) >"$log" 2>&1 || shell_status=$?

    if [ "$shell_status" -eq 0 ]; then
        row=$(awk -F '\t' '$1 == "backend" { line = $0 } END { print line }' "$log")
        if [ -z "$row" ]; then
            generator_litmus_status=failed
            failure_count=$((failure_count + 1))
        else
            IFS=$'\t' read -r _ _ _ _ _ _ time_s rss_kb scenario_ns _ <<< "$row"
            generator_litmus_status=capable
        fi
    elif [ "$shell_status" -eq 124 ]; then
        generator_litmus_status=observed-timeout
        observed_timeout_count=$((observed_timeout_count + 1))
    else
        generator_litmus_status=failed
        failure_count=$((failure_count + 1))
    fi

    record_measurement generator materialized-range "$GENERATOR_FACT_COUNT" "$GENERATOR_ADMISSION" \
        "$generator_litmus_status" "$time_s" "$rss_kb" "$scenario_ns"
}

prepare_corpus() {
    local fact_count=$1
    local corpus="$corpus_dir/friends_${fact_count}.metta"
    local corpus_tmp="$corpus.tmp"
    local act="$corpus_dir/friends_${fact_count}.act"
    local generator_log="$RUNTIME_DIR/corpus_${fact_count}.log"
    local act_log="$RUNTIME_DIR/act_${fact_count}.log"
    local shell_status=0

    /usr/bin/time -f 'CORPUS_GENERATOR_SECONDS=%e\nCORPUS_GENERATOR_RSS_KB=%M' \
        env LC_ALL=C awk -v count="$fact_count" \
            'BEGIN { for (i = 0; i < count; i++) print "(friend sam " i ")" }' \
            >"$corpus_tmp" 2>"$generator_log"
    mv "$corpus_tmp" "$corpus"
    printf 'CORPUS_ROWS=%s\nCORPUS_BYTES=%s\n' \
        "$fact_count" "$(wc -c <"$corpus")" >>"$generator_log"
    corpus_paths[$fact_count]=$corpus
    act_paths[$fact_count]=$act

    if needs_act_corpus; then
        (
            cd "$ROOT"
            timeout --kill-after=5 "$CASE_TIMEOUT_S" \
                env CETTA_BENCH_CORPUS_PATH="$corpus" CETTA_BENCH_ACT_PATH="$act" \
                    "$BACKEND_SCRIPT" prepare-act "$fact_count" 1
        ) >"$act_log" 2>&1 || shell_status=$?
        if [ "$shell_status" -ne 0 ] || [ ! -s "$act" ]; then
            printf 'ACT preparation failed for %s facts (status %s); see %s\n' \
                "$fact_count" "$shell_status" "$act_log" >&2
            return 1
        fi
    fi
}

run_case() {
    local suite_kind=$1
    local case_id=$2
    local fact_count=$3
    local admission=$4
    local corpus=${corpus_paths[$fact_count]}
    local act=${act_paths[$fact_count]}
    local log="$RUNTIME_DIR/${suite_kind}_${case_id}_${fact_count}.log"
    local shell_status=0
    local row=""
    local time_s=unknown
    local rss_kb=unknown
    local scenario_ns=unknown
    local term_universe_inserts=unknown
    local term_universe_blob_bytes=unknown
    local atom_id_capacity_bytes=unknown
    local pathmap_direct_store=unknown
    local mork_add_call=unknown
    local mork_add_batch_items=unknown
    local status=failed

    if [ "$fact_count" -eq "$base_size" ]; then
        base_case_count=$((base_case_count + 1))
    elif [ "$fact_count" -eq "$frontier_size" ]; then
        frontier_case_count=$((frontier_case_count + 1))
    fi

    if [ "$suite_kind" = "backend" ]; then
        (
            cd "$ROOT"
            /usr/bin/time -f 'outer_elapsed_sec=%e outer_rss_kb=%M' \
                timeout --kill-after=5 "$CASE_TIMEOUT_S" \
                env CETTA_BENCH_CORPUS_PATH="$corpus" CETTA_BENCH_ACT_PATH="$act" \
                    CETTA_BENCH_EMIT_RUNTIME_STATS=1 \
                    "$BACKEND_SCRIPT" "$case_id" "$fact_count" "$MATCH_ROUNDS" suite_total
        ) >"$log" 2>&1 || shell_status=$?
    else
        (
            cd "$ROOT"
            /usr/bin/time -f 'outer_elapsed_sec=%e outer_rss_kb=%M' \
                timeout --kill-after=5 "$CASE_TIMEOUT_S" \
                env CETTA_BENCH_CORPUS_PATH="$corpus" CETTA_BENCH_ACT_PATH="$act" \
                    CETTA_BENCH_EMIT_RUNTIME_STATS=1 \
                    "$TRANSFER_SCRIPT" "$case_id" "$fact_count" "$MATCH_ROUNDS" suite_total
        ) >"$log" 2>&1 || shell_status=$?
    fi

    if [ "$shell_status" -eq 0 ]; then
        row=$(awk -F '\t' -v kind="$suite_kind" '$1 == kind { line = $0 } END { print line }' "$log")
        if [ -n "$row" ]; then
            if [ "$suite_kind" = "backend" ]; then
                IFS=$'\t' read -r _ _ _ _ _ _ time_s rss_kb scenario_ns \
                    term_universe_inserts term_universe_blob_bytes \
                    atom_id_capacity_bytes pathmap_direct_store mork_add_call \
                    mork_add_batch_items <<< "$row"
            else
                IFS=$'\t' read -r _ _ _ _ _ _ _ _ _ time_s rss_kb scenario_ns \
                    term_universe_inserts term_universe_blob_bytes \
                    atom_id_capacity_bytes pathmap_direct_store mork_add_call \
                    mork_add_batch_items <<< "$row"
            fi
            if [ "$term_universe_inserts" = na ] || \
               [ "$term_universe_blob_bytes" = na ] || \
               [ "$atom_id_capacity_bytes" = na ] || \
               [ "$pathmap_direct_store" = na ] || \
               [ "$mork_add_call" = na ] || \
               [ "$mork_add_batch_items" = na ]; then
                printf 'Missing runtime counters for %s:%s/%s\n' \
                    "$suite_kind" "$case_id" "$fact_count" >&2
                failure_count=$((failure_count + 1))
            elif ! validate_route_counters "$suite_kind" "$case_id" "$fact_count" \
                    "$pathmap_direct_store" "$mork_add_call" \
                    "$mork_add_batch_items"; then
                failure_count=$((failure_count + 1))
            else
                status=pass
            fi
        else
            failure_count=$((failure_count + 1))
        fi
    elif [ "$shell_status" -eq 124 ]; then
        status=timeout
        timeout_count=$((timeout_count + 1))
        if [ "$admission" = "observed" ]; then
            observed_timeout_count=$((observed_timeout_count + 1))
        else
            failure_count=$((failure_count + 1))
        fi
    else
        status=failed
        failure_count=$((failure_count + 1))
    fi

    record_measurement "$suite_kind" "$case_id" "$fact_count" "$admission" \
        "$status" "$time_s" "$rss_kb" "$scenario_ns"
    if [ "$status" = "pass" ] && [ "$admission" = "required" ] && \
       [ "$fact_count" -eq "$frontier_size" ]; then
        printf 'SPACE_FRONTIER_WITNESS=space-frontier:%s:%s@%s\n' \
            "$suite_kind" "$case_id" "$fact_count"
        printf 'space-frontier:%s:%s@%s\n' \
            "$suite_kind" "$case_id" "$fact_count" >>"$realized_witnesses"
    fi
    LAST_STATUS=$status
    LAST_SCENARIO_NS=$scenario_ns
    LAST_RSS_KB=$rss_kb
}

exponent() {
    local base_value=$1
    local frontier_value=$2
    awk -v x0="$base_size" -v x1="$frontier_size" \
        -v y0="$base_value" -v y1="$frontier_value" \
        'BEGIN { printf "%.6f", log(y1 / y0) / log(x1 / x0) }'
}

exceeds() {
    awk -v actual="$1" -v limit="$2" 'BEGIN { exit !(actual > limit) }'
}

evaluate_growth() {
    local suite_kind=$1
    local case_id=$2
    local admission=$3
    local key="$suite_kind:$case_id"
    local time_exponent
    local rss_exponent
    local status=pass

    time_exponent=$(exponent "${base_ns[$key]}" "$LAST_SCENARIO_NS")
    rss_exponent=$(exponent "${base_rss[$key]}" "$LAST_RSS_KB")
    if exceeds "$time_exponent" "$MAX_TIME_EXPONENT" || \
       exceeds "$rss_exponent" "$MAX_RSS_EXPONENT"; then
        status=exceeds-bound
        failure_count=$((failure_count + 1))
    fi
    printf 'growth\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t-\n' \
        "$suite_kind" "$case_id" "$base_size" "$frontier_size" "$admission" \
        "$status" "$time_exponent" "$rss_exponent"
}

run_series() {
    local suite_kind=$1
    local case_id=$2
    local key="$suite_kind:$case_id"
    local frontier_admission=required
    local failures_before=$failure_count

    run_case "$suite_kind" "$case_id" "$base_size" required
    if [ "$LAST_STATUS" != "pass" ]; then
        record_measurement "$suite_kind" "$case_id" "$frontier_size" \
            "$frontier_admission" skipped-after-base-failure unknown unknown unknown
        if [ "$failure_count" -ne "$failures_before" ]; then
            return 1
        fi
        return 0
    fi
    base_ns[$key]=$LAST_SCENARIO_NS
    base_rss[$key]=$LAST_RSS_KB

    run_case "$suite_kind" "$case_id" "$frontier_size" "$frontier_admission"
    if [ "$LAST_STATUS" = "pass" ]; then
        evaluate_growth "$suite_kind" "$case_id" "$frontier_admission"
    fi
    [ "$failure_count" -eq "$failures_before" ]
}

validate_frontier_evidence() {
    local expected_witnesses="$corpus_dir/expected-frontier-witnesses.txt"
    local normalized_realized="$corpus_dir/realized-frontier-witnesses.sorted.txt"
    local evidence_status=passed
    local mode

    : >"$expected_witnesses"
    for mode in "${backend_modes[@]}"; do
        printf 'space-frontier:backend:%s@%s\n' "$mode" "$frontier_size" \
            >>"$expected_witnesses"
    done
    for mode in "${transfer_cases[@]}"; do
        printf 'space-frontier:transfer:%s@%s\n' "$mode" "$frontier_size" \
            >>"$expected_witnesses"
    done
    sort -u -o "$expected_witnesses" "$expected_witnesses"
    sort -u "$realized_witnesses" >"$normalized_realized"

    if [ "$base_case_count" -ne "$expected_case_count" ] || \
       [ "$frontier_case_count" -ne "$expected_case_count" ] || \
       ! cmp -s "$expected_witnesses" "$normalized_realized"; then
        printf 'Frontier execution set is incomplete: expected %s base and frontier cases, got %s and %s\n' \
            "$expected_case_count" "$base_case_count" "$frontier_case_count" >&2
        evidence_status=failed
    fi

    if ! PYTHONPATH="$ROOT/scripts${PYTHONPATH:+:$PYTHONPATH}" \
        python3 - "$PROPERTY_MANIFEST" "$normalized_realized" "$frontier_size" <<'PY'
import sys
from pathlib import Path

from cetta_readiness_model import (
    load_property_manifest,
    missing_frontier_witness_ids,
)

manifest = load_property_manifest(Path(sys.argv[1]))
realized = set(Path(sys.argv[2]).read_text(encoding="utf-8").splitlines())
missing = missing_frontier_witness_ids(manifest, realized, int(sys.argv[3]))
if missing:
    print("Missing manifest frontier witnesses: " + ", ".join(missing), file=sys.stderr)
    raise SystemExit(1)
PY
    then
        evidence_status=failed
    fi

    if [ "$evidence_status" != passed ]; then
        if [ "$failure_count" -eq 0 ]; then
            failure_count=1
        fi
        return 1
    fi
}

printf 'record\tkind\tcase_id\tsize_from\tsize_to\tadmission\tstatus\ttime_value\trss_value\tscenario_ns\n'

run_generator_litmus
if [ "$failure_count" -ne 0 ]; then
    print_summary
    exit 1
fi

for fact_count in "${sizes[@]}"; do
    if ! prepare_corpus "$fact_count"; then
        failure_count=$((failure_count + 1))
        print_summary
        exit 1
    fi
done

series_failed=0
for mode in "${backend_modes[@]}"; do
    if ! run_series backend "$mode"; then
        series_failed=1
        break
    fi
done
if [ "$series_failed" -eq 0 ]; then
    for case_id in "${transfer_cases[@]}"; do
        if ! run_series transfer "$case_id"; then
            break
        fi
    done
fi
validate_frontier_evidence || true
print_summary
test "$failure_count" -eq 0
