#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${CETTA_BIN:-$ROOT/cetta}"
MODE="${1:-all}"
FACT_COUNT="${2:-10000}"
MATCH_ROUNDS="${3:-3}"
SCENARIOS_STR="${4:-insert_only exact_hit_after_insert full_scan_after_insert post_remove_count_after_insert suite_total}"
RUNTIME_DIR="$ROOT/runtime/bench_space_backend"
ACT_PATH="${CETTA_BENCH_ACT_PATH:-$RUNTIME_DIR/mork_backend_${FACT_COUNT}.act}"
CORPUS_PATH="${CETTA_BENCH_CORPUS_PATH:-}"
RANGE_CHUNK="${CETTA_BENCH_RANGE_CHUNK:-100000}"
MATERIALIZED_RANGE="${CETTA_BENCH_MATERIALIZED_RANGE:-0}"

usage() {
    cat <<EOF
Usage: $(basename "$0") [all|compare|native|pathmap|mork-live|mork-open-act|mork-load-act|prepare-act] [FACT_COUNT] [MATCH_ROUNDS] [SCENARIOS]

Examples:
  ./scripts/bench_space_backend_matrix.sh all 10000 3
  ./scripts/bench_space_backend_matrix.sh native 100000 1 suite_total
EOF
}

die() {
    printf '%s\n' "$*" >&2
    exit 1
}

case "$MODE" in
    all|compare|native|pathmap|mork-live|mork-open-act|mork-load-act|prepare-act)
        ;;
    -h|--help)
        usage
        exit 0
        ;;
    *)
        die "Unknown mode: $MODE"
        ;;
esac

[[ "$FACT_COUNT" =~ ^[0-9]+$ ]] || die "FACT_COUNT must be a non-negative integer"
[[ "$MATCH_ROUNDS" =~ ^[0-9]+$ ]] || die "MATCH_ROUNDS must be a non-negative integer"
[ "$FACT_COUNT" -gt 42 ] || die "FACT_COUNT must be greater than 42 because the workload removes (friend sam 42)"
[ "$MATCH_ROUNDS" -gt 0 ] || die "MATCH_ROUNDS must be positive"
[ -x "$BIN" ] || die "Missing executable $BIN"
[[ "$RANGE_CHUNK" =~ ^[0-9]+$ ]] || die "CETTA_BENCH_RANGE_CHUNK must be a positive integer"
[ "$RANGE_CHUNK" -gt 0 ] || die "CETTA_BENCH_RANGE_CHUNK must be positive"
case "$MATERIALIZED_RANGE" in
    0|1) ;;
    *) die "CETTA_BENCH_MATERIALIZED_RANGE must be 0 or 1" ;;
esac
if [ -n "$CORPUS_PATH" ]; then
    [ -f "$CORPUS_PATH" ] || die "Missing corpus $CORPUS_PATH"
fi

mkdir -p "$RUNTIME_DIR"
tmp_dir=$(mktemp -d "$RUNTIME_DIR/.bench_space_backend.XXXXXX")
CORPUS_IMPORT_PATH=""
if [ -n "$CORPUS_PATH" ]; then
    ln -s "$(realpath -- "$CORPUS_PATH")" "$tmp_dir/friend_corpus.metta"
    CORPUS_IMPORT_PATH="friend_corpus.metta"
fi
preserve_tmp="${BENCH_KEEP_TMP:-0}"
cleanup() {
    if [ "$preserve_tmp" = "1" ]; then
        printf 'preserved_tmp\t%s\n' "$tmp_dir" >&2
        return
    fi
    rm -rf "$tmp_dir"
}
trap cleanup EXIT

preserve_failure_artifacts() {
    local mode=$1
    local scenario=$2
    local status=$3
    local file=$4
    local log=$5
    preserve_tmp=1
    printf 'backend_failed\tmode=%s\tscenario=%s\tstatus=%s\tfile=%s\tlog=%s\n' \
        "$mode" "$scenario" "$status" "$file" "$log" >&2
    if [ -s "$log" ]; then
        printf '%s\n' "--- tail $log ---" >&2
        tail -80 "$log" >&2 || true
        printf '%s\n' '--- end tail ---' >&2
    fi
}

run_cetta_file() {
    local file=$1
    local output=$2
    local space_engine=${3:-}
    local status=0
    local -a command=("$BIN")
    if [ "${CETTA_BENCH_EMIT_RUNTIME_STATS:-0}" = "1" ]; then
        command+=(--emit-runtime-stats)
    fi
    if [ -n "$space_engine" ]; then
        command+=(--space-engine "$space_engine")
    fi
    command+=(--quiet --profile he-extended --lang he "$file")
    (
        cd "$ROOT"
        /usr/bin/time -f 'time_sec=%e rss_kb=%M' "${command[@]}"
    ) >"$output" 2>&1 || status=$?
    return "$status"
}

mode_imports() {
    local mode=$1
    case "$mode" in
        native|pathmap)
            cat <<'EOF'
!(import! &self list)
!(import! &self system)
EOF
            ;;
        mork-live)
            cat <<'EOF'
!(import! &self list)
!(import! &self mork)
!(import! &self system)
EOF
            ;;
        mork-open-act|mork-load-act)
            cat <<'EOF'
!(import! &self mork)
!(import! &self system)
EOF
            ;;
        *)
            die "Unknown mode: $mode"
            ;;
    esac
}

mode_setup() {
    local mode=$1
    case "$mode" in
        native)
            if [ -z "$CORPUS_PATH" ]; then
                cat <<'EOF'
!(bind! &space (new-space native))
EOF
            fi
            ;;
        pathmap)
            cat <<'EOF'
!(bind! &space (new-space pathmap))
EOF
            ;;
        mork-live)
            cat <<'EOF'
!(bind! &space (mork:new-space))
EOF
            ;;
        mork-open-act)
            cat <<EOF
!(bind! &space (mork:open-act "$ACT_PATH"))
EOF
            ;;
        mork-load-act)
            cat <<EOF
!(bind! &space (mork:new-space))
!(mork:load-act! &space "$ACT_PATH")
EOF
            ;;
        *)
            die "Unknown mode: $mode"
            ;;
    esac
}

emit_range_inserts() {
    local mode=$1
    local start=0
    local stop
    local chunk=$RANGE_CHUNK

    if [ "$MATERIALIZED_RANGE" = "1" ]; then
        chunk=$FACT_COUNT
    fi
    while [ "$start" -lt "$FACT_COUNT" ]; do
        stop=$((start + chunk))
        if [ "$stop" -gt "$FACT_COUNT" ]; then
            stop=$FACT_COUNT
        fi
        case "$mode" in
            native|pathmap)
                cat <<EOF
!(let \$ignored
   (collapse
     (let \$xs (eval (list:range $start $stop))
       (let \$i (superpose \$xs)
         (add-atom &space (friend sam \$i)))))
   ())
EOF
                ;;
            mork-live)
                cat <<EOF
!(let \$ignored
   (collapse
     (let \$xs (eval (list:range $start $stop))
       (let \$i (superpose \$xs)
         (mork:add-atom &space (friend sam \$i)))))
   ())
EOF
                ;;
            *)
                die "Unsupported range-insert mode: $mode"
                ;;
        esac
        start=$stop
    done
}

mode_insert() {
    local mode=$1
    if [ -n "$CORPUS_PATH" ]; then
        case "$mode" in
            native)
                printf '!(import! &space "%s")\n' "$CORPUS_IMPORT_PATH"
                ;;
            pathmap)
                printf '!(import! &corpus "%s")\n' "$CORPUS_IMPORT_PATH"
                printf '!(add-atoms &space (get-atoms &corpus))\n'
                ;;
            mork-live)
                [ -f "$ACT_PATH" ] || die "Missing ACT corpus $ACT_PATH"
                printf '!(mork:load-act! &space "%s")\n' "$ACT_PATH"
                ;;
            mork-open-act|mork-load-act)
                [ -f "$ACT_PATH" ] || die "Missing ACT corpus $ACT_PATH"
                ;;
            *)
                die "Unknown mode: $mode"
                ;;
        esac
        return
    fi
    case "$mode" in
        native|pathmap)
            emit_range_inserts "$mode"
            ;;
        mork-live)
            emit_range_inserts "$mode"
            ;;
        mork-open-act|mork-load-act)
            ;;
        *)
            die "Unknown mode: $mode"
            ;;
    esac
}

space_size_expr() {
    local mode=$1
    case "$mode" in
        native|pathmap)
            printf '(size &space)\n'
            ;;
        mork-live|mork-open-act|mork-load-act)
            printf '(mork:size &space)\n'
            ;;
        *)
            die "Unknown mode: $mode"
            ;;
    esac
}

match_count_expr() {
    local mode=$1
    local pattern=$2
    local template=$3
    case "$mode" in
        native|pathmap)
            printf '(size (collapse (match &space %s %s)))\n' "$pattern" "$template"
            ;;
        mork-live|mork-open-act|mork-load-act)
            printf '(size (collapse (mork:match &space %s %s)))\n' "$pattern" "$template"
            ;;
        *)
            die "Unknown mode: $mode"
            ;;
    esac
}

remove_call() {
    local mode=$1
    case "$mode" in
        native|pathmap)
            printf '!(remove-atom &space (friend sam 42))\n'
            ;;
        mork-live|mork-open-act|mork-load-act)
            printf '!(mork:remove-atom &space (friend sam 42))\n'
            ;;
        *)
            die "Unknown mode: $mode"
            ;;
    esac
}

render_prepare_file() {
    local out=$1
    if [ -n "$CORPUS_PATH" ]; then
        cat >"$out" <<EOF
!(import! &self mork)
!(import! &corpus "$CORPUS_IMPORT_PATH")
!(bind! &space (mork:new-space))
!(mork:add-atoms &space (get-atoms &corpus))
!(assertEqualToResult (mork:size &space) ($FACT_COUNT))
!(mork:dump! &space "$ACT_PATH")
!(println! (bench prepared_rows $FACT_COUNT))
EOF
        return
    fi
    {
        cat <<'EOF'
!(import! &self list)
!(import! &self mork)
!(bind! &space (mork:new-space))
EOF
        emit_range_inserts mork-live
        cat <<EOF
!(assertEqualToResult (mork:size &space) ($FACT_COUNT))
!(mork:dump! &space "$ACT_PATH")
!(println! (bench prepared_rows $FACT_COUNT))
EOF
    } >"$out"
}

scenario_program_path() {
    local mode=$1
    local scenario=$2
    printf '%s/%s_%s_%s.metta\n' "$tmp_dir" "$mode" "$scenario" "$FACT_COUNT"
}

scenario_log_path() {
    local mode=$1
    local scenario=$2
    printf '%s/%s_%s_%s.log\n' "$tmp_dir" "$mode" "$scenario" "$FACT_COUNT"
}

render_program() {
    local mode=$1
    local scenario=$2
    local out=$3
    local expected_remaining=$((FACT_COUNT - 1))
    local expected_full_scan=$((FACT_COUNT * MATCH_ROUNDS))

    {
        mode_imports "$mode"
        printf '!(bind! &bench-start-ns (system:monotonic-ns))\n'
        mode_setup "$mode"
        mode_insert "$mode"

        printf '!(assertEqualToResult %s (%s))\n' "$(space_size_expr "$mode")" "$FACT_COUNT"
        printf '!(println! (bench initial_size_rows %s))\n' "$(space_size_expr "$mode")"

        case "$scenario" in
            insert_only)
                printf '!(println! (bench scenario_rows %s))\n' "$(space_size_expr "$mode")"
                ;;
            exact_hit_after_insert)
                printf '!(let $rows %s\n' "$(match_count_expr "$mode" '(friend sam 42)' '(friend sam 42)')"
                printf '   (println! (bench scenario_rows $rows)))\n'
                printf '!(assertEqualToResult %s (1))\n' "$(match_count_expr "$mode" '(friend sam 42)' '(friend sam 42)')"
                ;;
            full_scan_after_insert)
                printf '!(bind! &acc (new-state 0))\n'
                for _ in $(seq 1 "$MATCH_ROUNDS"); do
                    printf '!(let $rows %s\n' "$(match_count_expr "$mode" '(friend $y $x)' '(friend $y $x)')"
                    printf '   (change-state! &acc (+ (get-state &acc) $rows)))\n'
                done
                printf '!(println! (bench scenario_rows (get-state &acc)))\n'
                printf '!(assertEqualToResult (get-state &acc) (%s))\n' "$expected_full_scan"
                ;;
            post_remove_count_after_insert)
                remove_call "$mode"
                printf '!(assertEqualToResult %s (%s))\n' "$(space_size_expr "$mode")" "$expected_remaining"
                printf '!(let $rows %s\n' "$(match_count_expr "$mode" '(friend $y $x)' '(friend $y $x)')"
                printf '   (println! (bench scenario_rows $rows)))\n'
                printf '!(assertEqualToResult %s (%s))\n' "$(match_count_expr "$mode" '(friend $y $x)' '(friend $y $x)')" "$expected_remaining"
                ;;
            suite_total)
                printf '!(assertEqualToResult %s (1))\n' "$(match_count_expr "$mode" '(friend sam 42)' '(friend sam 42)')"
                printf '!(bind! &acc (new-state 0))\n'
                for _ in $(seq 1 "$MATCH_ROUNDS"); do
                    printf '!(let $rows %s\n' "$(match_count_expr "$mode" '(friend $y $x)' '(friend $y $x)')"
                    printf '   (change-state! &acc (+ (get-state &acc) $rows)))\n'
                done
                printf '!(assertEqualToResult (get-state &acc) (%s))\n' "$expected_full_scan"
                remove_call "$mode"
                printf '!(assertEqualToResult %s (%s))\n' "$(space_size_expr "$mode")" "$expected_remaining"
                printf '!(let $rows %s\n' "$(match_count_expr "$mode" '(friend $y $x)' '(friend $y $x)')"
                printf '   (println! (bench scenario_rows $rows)))\n'
                printf '!(assertEqualToResult %s (%s))\n' "$(match_count_expr "$mode" '(friend $y $x)' '(friend $y $x)')" "$expected_remaining"
                ;;
            *)
                die "Unknown scenario: $scenario"
                ;;
        esac
        printf '!(let $bench-finished-ns (system:monotonic-ns)\n'
        printf '   (println! (bench scenario_ns (- $bench-finished-ns &bench-start-ns))))\n'
    } >"$out"
}

extract_metric() {
    local key=$1
    local log=$2
    awk -v want="$key" '
        /^\(bench / {
            gsub(/[()]/, "", $0);
            if ($2 == want) {
                print $3;
                exit;
            }
        }
    ' "$log"
}

extract_time() {
    local log=$1
    grep 'time_sec=' "$log" | tail -1 | sed -E 's/.*time_sec=([0-9.]+).*/\1/'
}

extract_rss() {
    local log=$1
    grep 'rss_kb=' "$log" | tail -1 | sed -E 's/.*rss_kb=([0-9]+).*/\1/'
}

extract_counter() {
    local name=$1
    local log=$2
    awk -v want="$name" '
        $1 == "runtime-counter" && $2 == want { value = $3 }
        END { print value == "" ? "na" : value }
    ' "$log"
}

expect_value() {
    local label=$1
    local expected=$2
    local actual=$3
    if [ "$actual" != "$expected" ]; then
        printf 'unexpected %s: expected %s, got %s\n' \
            "$label" "$expected" "${actual:-<missing>}" >&2
        exit 1
    fi
}

prepare_act() {
    local file="$tmp_dir/prepare.metta"
    local log="$tmp_dir/prepare.log"
    mkdir -p "$(dirname -- "$ACT_PATH")"
    render_prepare_file "$file"
    if run_cetta_file "$file" "$log"; then
        :
    else
        local status=$?
        preserve_failure_artifacts "prepare-act" "prepare" "$status" "$file" "$log"
        exit "$status"
    fi
    local prepared
    prepared=$(extract_metric prepared_rows "$log")
    expect_value "prepare prepared_rows" "$FACT_COUNT" "$prepared"
}

run_mode_scenario() {
    local mode=$1
    local scenario=$2
    local expected
    local file
    local log
    local initial_size
    local scenario_rows
    local time_sec
    local rss_kb
    local scenario_ns
    local term_universe_inserts
    local term_universe_blob_bytes
    local atom_id_capacity_bytes
    local pathmap_direct_store
    local mork_add_call
    local mork_add_batch_items
    local default_space_engine=""

    file=$(scenario_program_path "$mode" "$scenario")
    log=$(scenario_log_path "$mode" "$scenario")
    if [ "$mode" = native ]; then
        default_space_engine=native
    fi
    render_program "$mode" "$scenario" "$file"
    if run_cetta_file "$file" "$log" "$default_space_engine"; then
        :
    else
        local status=$?
        preserve_failure_artifacts "$mode" "$scenario" "$status" "$file" "$log"
        exit "$status"
    fi

    initial_size=$(extract_metric initial_size_rows "$log")
    scenario_rows=$(extract_metric scenario_rows "$log")
    time_sec=$(extract_time "$log")
    rss_kb=$(extract_rss "$log")
    scenario_ns=$(extract_metric scenario_ns "$log")
    term_universe_inserts=$(extract_counter term-universe-insert "$log")
    term_universe_blob_bytes=$(extract_counter term-universe-blob-bytes "$log")
    atom_id_capacity_bytes=$(extract_counter space-atom-id-capacity-bytes-peak "$log")
    pathmap_direct_store=$(extract_counter pathmap-direct-store "$log")
    mork_add_call=$(extract_counter mork-add-call "$log")
    mork_add_batch_items=$(extract_counter mork-add-batch-items "$log")

    case "$scenario" in
        insert_only) expected="$FACT_COUNT" ;;
        exact_hit_after_insert) expected="1" ;;
        full_scan_after_insert) expected="$((FACT_COUNT * MATCH_ROUNDS))" ;;
        post_remove_count_after_insert|suite_total) expected="$((FACT_COUNT - 1))" ;;
        *) die "Unknown scenario: $scenario" ;;
    esac

    expect_value "$mode/$scenario initial_size_rows" "$FACT_COUNT" "$initial_size"
    expect_value "$mode/$scenario scenario_rows" "$expected" "$scenario_rows"
    [ -n "$time_sec" ] || die "missing timing for $mode/$scenario"
    [ -n "$rss_kb" ] || die "missing RSS for $mode/$scenario"
    [ -n "$scenario_ns" ] || die "missing scenario_ns for $mode/$scenario"

    printf 'backend\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$mode" "$scenario" "$FACT_COUNT" "$MATCH_ROUNDS" "$scenario_rows" \
        "$time_sec" "$rss_kb" "$scenario_ns" "$term_universe_inserts" \
        "$term_universe_blob_bytes" "$atom_id_capacity_bytes" \
        "$pathmap_direct_store" "$mork_add_call" "$mork_add_batch_items"
}

IFS=' ' read -r -a scenarios <<< "$SCENARIOS_STR"
if [ "$MODE" = "prepare-act" ]; then
    prepare_act
    printf 'prepared-act\t%s\t%s\n' "$FACT_COUNT" "$ACT_PATH"
    exit 0
fi
selected_modes=()
case "$MODE" in
    all)
        selected_modes=(native pathmap mork-live mork-open-act mork-load-act)
        ;;
    compare)
        selected_modes=(native pathmap mork-live)
        ;;
    *)
        selected_modes=("$MODE")
        ;;
esac

for mode in "${selected_modes[@]}"; do
    if [ "$mode" = "mork-open-act" ] || [ "$mode" = "mork-load-act" ]; then
        if [ ! -f "$ACT_PATH" ]; then
            prepare_act
        fi
        break
    fi
done

printf 'kind\tmode\tscenario\tfact_count\tmatch_rounds\tcount\ttime_s\trss_kb\tscenario_ns\tterm_universe_inserts\tterm_universe_blob_bytes\tspace_atom_id_capacity_bytes_peak\tpathmap_direct_store\tmork_add_call\tmork_add_batch_items\n'
for mode in "${selected_modes[@]}"; do
    for scenario in "${scenarios[@]}"; do
        run_mode_scenario "$mode" "$scenario"
    done
done
