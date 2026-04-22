#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/cetta"
MODE="${1:-all}"
FACT_COUNT="${2:-10000}"
MATCH_ROUNDS="${3:-3}"
SCENARIOS_STR="${4:-insert_only exact_hit_after_insert full_scan_after_insert post_remove_count_after_insert suite_total}"
RUNTIME_DIR="$ROOT/runtime/bench_space_backend"
ACT_PATH="$RUNTIME_DIR/mork_backend_${FACT_COUNT}.act"

usage() {
    cat <<EOF
Usage: $(basename "$0") [all|compare|native|pathmap|mork-live|mork-open-act|mork-load-act] [FACT_COUNT] [MATCH_ROUNDS] [SCENARIOS]

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
    all|compare|native|pathmap|mork-live|mork-open-act|mork-load-act)
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

mkdir -p "$RUNTIME_DIR"
tmp_dir=$(mktemp -d "$RUNTIME_DIR/.bench_space_backend.XXXXXX")
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
    local status=0
    /usr/bin/time -f 'time_sec=%e rss_kb=%M' \
        bash -lc "ulimit -v 10485760; ulimit -s unlimited; cd '$ROOT'; '$BIN' --quiet --lang he '$file'" \
        >"$output" 2>&1 || status=$?
    return "$status"
}

mode_imports() {
    local mode=$1
    case "$mode" in
        native|pathmap)
            cat <<'EOF'
!(import! &self list)
EOF
            ;;
        mork-live)
            cat <<'EOF'
!(import! &self list)
!(import! &self mork)
EOF
            ;;
        mork-open-act|mork-load-act)
            cat <<'EOF'
!(import! &self mork)
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
            cat <<'EOF'
!(bind! &space (new-space native))
EOF
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

mode_insert() {
    local mode=$1
    case "$mode" in
        native|pathmap)
            cat <<EOF
!(let \$ignored
   (collapse
     (let \$xs (eval (list:range $FACT_COUNT))
       (let \$i (superpose \$xs)
         (add-atom &space (friend sam \$i)))))
   ())
EOF
            ;;
        mork-live)
            cat <<EOF
!(let \$ignored
   (collapse
     (let \$xs (eval (list:range $FACT_COUNT))
       (let \$i (superpose \$xs)
         (mork:add-atom &space (friend sam \$i)))))
   ())
EOF
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
    cat >"$out" <<EOF
!(import! &self list)
!(import! &self mork)
!(bind! &space (mork:new-space))
!(let \$ignored
   (collapse
     (let \$xs (eval (list:range $FACT_COUNT))
       (let \$i (superpose \$xs)
         (mork:add-atom &space (friend sam \$i)))))
   ())
!(assertEqualToResult (mork:size &space) ($FACT_COUNT))
!(mork:dump! &space "$ACT_PATH")
!(println! (bench prepared_rows $FACT_COUNT))
EOF
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

    file=$(scenario_program_path "$mode" "$scenario")
    log=$(scenario_log_path "$mode" "$scenario")
    render_program "$mode" "$scenario" "$file"
    if run_cetta_file "$file" "$log"; then
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

    printf 'backend\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$mode" "$scenario" "$FACT_COUNT" "$MATCH_ROUNDS" "$scenario_rows" "$time_sec" "$rss_kb"
}

IFS=' ' read -r -a scenarios <<< "$SCENARIOS_STR"
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

printf 'kind\tmode\tscenario\tfact_count\tmatch_rounds\tcount\ttime_s\trss_kb\n'
for mode in "${selected_modes[@]}"; do
    for scenario in "${scenarios[@]}"; do
        run_mode_scenario "$mode" "$scenario"
    done
done
