#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
BIN="$ROOT/cetta"
MODE=${1:-all}
FACT_COUNT=${2:-100000}
MATCH_ROUNDS=${3:-3}
RUNTIME_DIR="$ROOT/runtime/bench_roman_space"
ACT_PATH="$RUNTIME_DIR/mork_live_${FACT_COUNT}.act"

usage() {
    cat <<EOF
Usage: $(basename "$0") [prepare|native-live|mork-live|mork-live-bulk-stream|mork-live-bulk-direct-stream|mork-live-bulk-materialized|mork-open-act|mork-load-act|all|all-bulk] [FACT_COUNT] [MATCH_ROUNDS]

Examples:
  ./scripts/bench_roman_space_workload.sh all 10000 3
  ./scripts/bench_roman_space_workload.sh all-bulk 10000 3
  ./scripts/bench_roman_space_workload.sh mork-live 100000 1
  ./scripts/bench_roman_space_workload.sh mork-live-bulk-stream 100000 1
  ./scripts/bench_roman_space_workload.sh mork-live-bulk-direct-stream 100000 1
  ./scripts/bench_roman_space_workload.sh mork-open-act 100000 3
EOF
}

die() {
    printf '%s\n' "$*" >&2
    exit 1
}

case "$MODE" in
    prepare|native-live|mork-live|mork-live-bulk-stream|mork-live-bulk-direct-stream|mork-live-bulk-materialized|mork-open-act|mork-load-act|all|all-bulk)
        ;;
    -h|--help)
        usage
        exit 0
        ;;
    *)
        die "Unknown mode: $MODE"
        ;;
esac

[ -x "$BIN" ] || die "Missing executable $BIN"
[[ "$FACT_COUNT" =~ ^[0-9]+$ ]] || die "FACT_COUNT must be a non-negative integer"
[[ "$MATCH_ROUNDS" =~ ^[0-9]+$ ]] || die "MATCH_ROUNDS must be a non-negative integer"
[ "$FACT_COUNT" -gt 42 ] || die "FACT_COUNT must be greater than 42 because the workload removes (friend sam 42)"
[ "$MATCH_ROUNDS" -gt 0 ] || die "MATCH_ROUNDS must be positive"

mkdir -p "$RUNTIME_DIR"

tmp_dir=$(mktemp -d "$RUNTIME_DIR/.bench_roman_space.XXXXXX")
cleanup() {
    rm -rf "$tmp_dir"
}
trap cleanup EXIT

run_cetta_file() {
    local file=$1
    local output=$2
    local status=0
    /usr/bin/time -f 'time_sec=%e rss_kb=%M' \
        bash -lc "ulimit -v 10485760; ulimit -s unlimited; '$BIN' --quiet --lang he '$file'" \
        >"$output" 2>&1 || status=$?
    return "$status"
}

mode_imports() {
    local mode=$1
    case "$mode" in
        native-live)
            cat <<'EOF'
!(import! &self list)
EOF
            ;;
        mork-live|mork-live-bulk-stream|mork-live-bulk-direct-stream|mork-live-bulk-materialized)
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
        native-live)
            cat <<'EOF'
!(bind! &space (new-space))
EOF
            ;;
        mork-live|mork-live-bulk-stream|mork-live-bulk-direct-stream|mork-live-bulk-materialized)
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

mode_insert_stream() {
    local mode=$1
    case "$mode" in
        native-live)
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
        mork-live-bulk-stream)
            cat <<EOF
!(mork:add-atoms &space
   (collapse
     (let \$xs (eval (list:range $FACT_COUNT))
       (let \$i (superpose \$xs)
         (friend sam \$i)))))
EOF
            ;;
        mork-live-bulk-direct-stream)
            cat <<EOF
!(__cetta_lib_mork_space_add_stream &space
   (let \$xs (eval (list:range $FACT_COUNT))
     (let \$i (superpose \$xs)
       (friend sam \$i))))
EOF
            ;;
        mork-live-bulk-materialized)
            cat <<EOF
!(let \$atoms
   (collapse
     (let \$xs (eval (list:range $FACT_COUNT))
       (let \$i (superpose \$xs)
         (friend sam \$i))))
   (mork:add-atoms &space \$atoms))
EOF
            ;;
        mork-open-act|mork-load-act)
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
        native-live)
            printf '(size (collapse (match &space %s %s)))\n' "$pattern" "$template"
            ;;
        mork-live|mork-live-bulk-stream|mork-live-bulk-direct-stream|mork-live-bulk-materialized|mork-open-act|mork-load-act)
            printf '(size (collapse (mork:match &space %s %s)))\n' "$pattern" "$template"
            ;;
        *)
            die "Unknown mode: $mode"
            ;;
    esac
}

bounded_match_count_expr() {
    local mode=$1
    local limit=$2
    local pattern=$3
    local template=$4
    case "$mode" in
        native-live)
            if [ "$limit" -eq 1 ]; then
                printf '(size (collapse (select %s (match &space %s %s))))\n' \
                    "$limit" "$pattern" "$template"
            else
                printf '(size (select %s (match &space %s %s)))\n' \
                    "$limit" "$pattern" "$template"
            fi
            ;;
        mork-live|mork-live-bulk-stream|mork-live-bulk-direct-stream|mork-live-bulk-materialized|mork-open-act|mork-load-act)
            if [ "$limit" -eq 1 ]; then
                printf '(size (collapse (select %s (mork:match &space %s %s))))\n' \
                    "$limit" "$pattern" "$template"
            else
                printf '(size (select %s (mork:match &space %s %s)))\n' \
                    "$limit" "$pattern" "$template"
            fi
            ;;
        *)
            die "Unknown mode: $mode"
            ;;
    esac
}

space_size_expr() {
    local mode=$1
    case "$mode" in
        native-live)
            printf '(size &space)\n'
            ;;
        mork-live|mork-live-bulk-stream|mork-live-bulk-direct-stream|mork-live-bulk-materialized|mork-open-act|mork-load-act)
            printf '(mork:size &space)\n'
            ;;
        *)
            die "Unknown mode: $mode"
            ;;
    esac
}

remove_call() {
    local mode=$1
    case "$mode" in
        native-live)
            printf '!(remove-atom &space (friend sam 42))\n'
            ;;
        mork-live|mork-live-bulk-stream|mork-live-bulk-direct-stream|mork-live-bulk-materialized|mork-open-act|mork-load-act)
            printf '!(mork:remove-atom &space (friend sam 42))\n'
            ;;
        *)
            die "Unknown mode: $mode"
            ;;
    esac
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

render_program() {
    local mode=$1
    local scenario=$2
    local out=$3
    local expected_remaining=$((FACT_COUNT - 1))

    {
        mode_imports "$mode"
        mode_setup "$mode"
        mode_insert_stream "$mode"

        printf '!(assertEqualToResult %s (%s))\n' "$(space_size_expr "$mode")" "$FACT_COUNT"
        printf '!(println! (bench initial_size_rows %s))\n' "$(space_size_expr "$mode")"

        case "$scenario" in
            insert_only)
                printf '!(println! (bench scenario_rows %s))\n' "$(space_size_expr "$mode")"
                ;;
            exact_hit_after_insert)
                printf '!(println! (bench scenario_rows %s))\n' \
                    "$(match_count_expr "$mode" '(friend sam 42)' '(friend sam 42)')"
                ;;
            exact_miss_after_insert)
                printf '!(println! (bench scenario_rows %s))\n' \
                    "$(match_count_expr "$mode" "(friend sam $FACT_COUNT)" "(friend sam $FACT_COUNT)")"
                ;;
            full_scan_after_insert)
                printf '!(bind! &acc (new-state 0))\n'
                for _ in $(seq 1 "$MATCH_ROUNDS"); do
                    printf '!(let $rows %s\n' \
                        "$(match_count_expr "$mode" '(friend $y $x)' '(friend $y $x)')"
                    printf '   (let $sum (+ (get-state &acc) $rows)\n'
                    printf '      (change-state! &acc $sum)))\n'
                done
                printf '!(println! (bench scenario_rows (get-state &acc)))\n'
                ;;
            select1_after_insert)
                printf '!(println! (bench scenario_rows %s))\n' \
                    "$(bounded_match_count_expr "$mode" 1 '(friend $y $x)' '(friend $y $x)')"
                ;;
            select5_after_insert)
                printf '!(println! (bench scenario_rows %s))\n' \
                    "$(bounded_match_count_expr "$mode" 5 '(friend $y $x)' '(friend $y $x)')"
                ;;
            remove_after_insert)
                remove_call "$mode"
                printf '!(println! (bench scenario_rows %s))\n' "$(space_size_expr "$mode")"
                ;;
            post_remove_exact_after_insert)
                remove_call "$mode"
                printf '!(println! (bench scenario_rows %s))\n' \
                    "$(match_count_expr "$mode" '(friend sam 42)' '(friend sam 42)')"
                ;;
            post_remove_count_after_insert)
                remove_call "$mode"
                printf '!(println! (bench scenario_rows %s))\n' \
                    "$(match_count_expr "$mode" '(friend $y $x)' '(friend $y $x)')"
                ;;
            suite_total)
                printf '!(bind! &acc (new-state 0))\n'
                printf '!(let $rows %s (change-state! &acc $rows))\n' \
                    "$(match_count_expr "$mode" '(friend sam 42)' '(friend sam 42)')"
                printf '!(let $rows %s (change-state! &acc $rows))\n' \
                    "$(match_count_expr "$mode" "(friend sam $FACT_COUNT)" "(friend sam $FACT_COUNT)")"
                for _ in $(seq 1 "$MATCH_ROUNDS"); do
                    printf '!(let $rows %s\n' \
                        "$(match_count_expr "$mode" '(friend $y $x)' '(friend $y $x)')"
                    printf '   (let $sum (+ (get-state &acc) $rows)\n'
                    printf '      (change-state! &acc $sum)))\n'
                done
                remove_call "$mode"
                printf '!(let $rows %s (change-state! &acc $rows))\n' \
                    "$(match_count_expr "$mode" '(friend sam 42)' '(friend sam 42)')"
                printf '!(println! (bench scenario_rows %s))\n' \
                    "$(match_count_expr "$mode" '(friend $y $x)' '(friend $y $x)')"
                ;;
            *)
                die "Unknown scenario: $scenario"
                ;;
        esac
    } >"$out"
}

prepare_act() {
    local file="$tmp_dir/prepare.metta"
    local log="$tmp_dir/prepare.log"
    render_prepare_file "$file"
    run_cetta_file "$file" "$log"
    local prepared
    prepared=$(awk '/^\(bench prepared_rows / { gsub(/[()]/, "", $0); print $3; exit }' "$log")
    [ "$prepared" = "$FACT_COUNT" ] || {
        printf 'ACT preparation failed for %s rows\n' "$FACT_COUNT" >&2
        cat "$log" >&2
        exit 1
    }
    printf 'prepared ACT: %s\n' "$ACT_PATH"
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

ratio() {
    local numerator=$1
    local denominator=$2
    awk -v n="$numerator" -v d="$denominator" 'BEGIN { if (d > 0) printf "%.2fx", n / d }'
}

run_mode_scenario() {
    local mode=$1
    local scenario=$2
    local file
    local log
    file=$(scenario_program_path "$mode" "$scenario")
    log=$(scenario_log_path "$mode" "$scenario")
    render_program "$mode" "$scenario" "$file"
    run_cetta_file "$file" "$log"

    local initial_size
    local scenario_rows
    local time_sec
    local rss_kb
    initial_size=$(extract_metric initial_size_rows "$log")
    scenario_rows=$(extract_metric scenario_rows "$log")
    time_sec=$(extract_time "$log")
    rss_kb=$(extract_rss "$log")

    expect_value "$mode/$scenario initial_size_rows" "$(
        case "$scenario" in
            insert_only|exact_hit_after_insert|exact_miss_after_insert|full_scan_after_insert|select1_after_insert|select5_after_insert) printf '%s' "$FACT_COUNT" ;;
            remove_after_insert|post_remove_exact_after_insert|post_remove_count_after_insert|suite_total) printf '%s' "$FACT_COUNT" ;;
        esac
    )" "$initial_size"
    expect_value "$mode/$scenario scenario_rows" "${expected_counts[$scenario]}" "$scenario_rows"
    [ -n "$time_sec" ] || die "missing timing for $mode/$scenario"
    [ -n "$rss_kb" ] || die "missing RSS for $mode/$scenario"

    printf '%s %s %s %s\n' "$scenario_rows" "$time_sec" "$rss_kb" "$log"
}

declare -A expected_counts=(
    [insert_only]="$FACT_COUNT"
    [exact_hit_after_insert]="1"
    [exact_miss_after_insert]="0"
    [full_scan_after_insert]="$((FACT_COUNT * MATCH_ROUNDS))"
    [select1_after_insert]="1"
    [select5_after_insert]="5"
    [remove_after_insert]="$((FACT_COUNT - 1))"
    [post_remove_exact_after_insert]="0"
    [post_remove_count_after_insert]="$((FACT_COUNT - 1))"
    [suite_total]="$((FACT_COUNT - 1))"
)

scenarios=(
    insert_only
    exact_hit_after_insert
    exact_miss_after_insert
    full_scan_after_insert
    select1_after_insert
    select5_after_insert
    remove_after_insert
    post_remove_exact_after_insert
    post_remove_count_after_insert
    suite_total
)

selected_modes=()
case "$MODE" in
    prepare)
        prepare_act
        exit 0
        ;;
    native-live)
        selected_modes=(native-live)
        ;;
    mork-live)
        selected_modes=(mork-live)
        ;;
    mork-live-bulk-stream)
        selected_modes=(mork-live-bulk-stream)
        ;;
    mork-live-bulk-direct-stream)
        selected_modes=(mork-live-bulk-direct-stream)
        ;;
    mork-live-bulk-materialized)
        selected_modes=(mork-live-bulk-materialized)
        ;;
    mork-open-act)
        selected_modes=(mork-open-act)
        ;;
    mork-load-act)
        selected_modes=(mork-load-act)
        ;;
    all)
        selected_modes=(native-live mork-live mork-open-act mork-load-act)
        ;;
    all-bulk)
        selected_modes=(native-live mork-live mork-live-bulk-stream mork-live-bulk-direct-stream mork-live-bulk-materialized)
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

declare -A counts=()
declare -A times=()
declare -A rss=()

for mode in "${selected_modes[@]}"; do
    for scenario in "${scenarios[@]}"; do
        read -r count time_sec rss_kb _ < <(run_mode_scenario "$mode" "$scenario")
        counts["$mode:$scenario"]=$count
        times["$mode:$scenario"]=$time_sec
        rss["$mode:$scenario"]=$rss_kb
    done
done

printf 'Roman-style CeTTa+MM2 workload: %s facts, %s repeated full scans\n' \
    "$FACT_COUNT" "$MATCH_ROUNDS"
printf 'Generated files live under %s\n' "$RUNTIME_DIR"
if [ -f "$ACT_PATH" ]; then
    printf 'ACT cache: %s\n' "$ACT_PATH"
    printf 'Note: open/load modes reuse the cached ACT and include per-scenario open/load cost,\n'
    printf 'but exclude the one-time live generation+dump used to prepare that ACT.\n'
fi
printf '\n'

if [ "${#selected_modes[@]}" -eq 1 ]; then
    mode=${selected_modes[0]}
    printf '%-30s %12s %12s %12s\n' "scenario" "time_s" "rss_kb" "count"
    printf '%-30s %12s %12s %12s\n' \
        "------------------------------" "------------" "------------" "------------"
    for scenario in "${scenarios[@]}"; do
        printf '%-30s %12s %12s %12s\n' \
            "$scenario" \
            "${times[$mode:$scenario]}" \
            "${rss[$mode:$scenario]}" \
            "${counts[$mode:$scenario]}"
    done
    exit 0
fi

if [ "$MODE" = "all" ]; then
    printf '%-30s %10s %10s %10s %10s %11s %11s %11s\n' \
        "scenario" "native_s" "mork_s" "open_s" "load_s" "mork/nat" "open/nat" "load/nat"
    printf '%-30s %10s %10s %10s %10s %11s %11s %11s\n' \
        "------------------------------" "----------" "----------" "----------" "----------" "-----------" "-----------" "-----------"

    for scenario in "${scenarios[@]}"; do
        printf '%-30s %10s %10s %10s %10s %11s %11s %11s\n' \
            "$scenario" \
            "${times[native-live:$scenario]}" \
            "${times[mork-live:$scenario]}" \
            "${times[mork-open-act:$scenario]}" \
            "${times[mork-load-act:$scenario]}" \
            "$(ratio "${times[mork-live:$scenario]}" "${times[native-live:$scenario]}")" \
            "$(ratio "${times[mork-open-act:$scenario]}" "${times[native-live:$scenario]}")" \
            "$(ratio "${times[mork-load-act:$scenario]}" "${times[native-live:$scenario]}")"
    done

    printf '\n'
    printf '%-30s %12s %12s %12s %12s %18s\n' \
        "scenario" "native_rss" "mork_rss" "open_rss" "load_rss" "counts"
    printf '%-30s %12s %12s %12s %12s %18s\n' \
        "------------------------------" "------------" "------------" "------------" "------------" "------------------"
    for scenario in "${scenarios[@]}"; do
        printf '%-30s %12s %12s %12s %12s %18s\n' \
            "$scenario" \
            "${rss[native-live:$scenario]}" \
            "${rss[mork-live:$scenario]}" \
            "${rss[mork-open-act:$scenario]}" \
            "${rss[mork-load-act:$scenario]}" \
            "${counts[native-live:$scenario]}/${counts[mork-live:$scenario]}/${counts[mork-open-act:$scenario]}/${counts[mork-load-act:$scenario]}"
    done
    exit 0
fi

printf 'Bulk-ingress comparison: native singular vs explicit MORK singular/batch live construction\n'
printf '%-30s %10s %10s %10s %10s %10s %11s %11s %11s %11s\n' \
    "scenario" "native_s" "mork_s" "bulk_str" "bulk_dir" "bulk_mat" "mork/nat" "str/nat" "dir/nat" "mat/nat"
printf '%-30s %10s %10s %10s %10s %10s %11s %11s %11s %11s\n' \
    "------------------------------" "----------" "----------" "----------" "----------" "----------" "-----------" "-----------" "-----------" "-----------"

for scenario in "${scenarios[@]}"; do
    printf '%-30s %10s %10s %10s %10s %10s %11s %11s %11s %11s\n' \
        "$scenario" \
        "${times[native-live:$scenario]}" \
        "${times[mork-live:$scenario]}" \
        "${times[mork-live-bulk-stream:$scenario]}" \
        "${times[mork-live-bulk-direct-stream:$scenario]}" \
        "${times[mork-live-bulk-materialized:$scenario]}" \
        "$(ratio "${times[mork-live:$scenario]}" "${times[native-live:$scenario]}")" \
        "$(ratio "${times[mork-live-bulk-stream:$scenario]}" "${times[native-live:$scenario]}")" \
        "$(ratio "${times[mork-live-bulk-direct-stream:$scenario]}" "${times[native-live:$scenario]}")" \
        "$(ratio "${times[mork-live-bulk-materialized:$scenario]}" "${times[native-live:$scenario]}")"
done

printf '\n'
printf '%-30s %12s %12s %12s %12s %12s %24s\n' \
    "scenario" "native_rss" "mork_rss" "bulk_str" "bulk_dir" "bulk_mat" "counts"
printf '%-30s %12s %12s %12s %12s %12s %24s\n' \
    "------------------------------" "------------" "------------" "------------" "------------" "------------" "------------------------"
for scenario in "${scenarios[@]}"; do
    printf '%-30s %12s %12s %12s %12s %12s %24s\n' \
        "$scenario" \
        "${rss[native-live:$scenario]}" \
        "${rss[mork-live:$scenario]}" \
        "${rss[mork-live-bulk-stream:$scenario]}" \
        "${rss[mork-live-bulk-direct-stream:$scenario]}" \
        "${rss[mork-live-bulk-materialized:$scenario]}" \
        "${counts[native-live:$scenario]}/${counts[mork-live:$scenario]}/${counts[mork-live-bulk-stream:$scenario]}/${counts[mork-live-bulk-direct-stream:$scenario]}/${counts[mork-live-bulk-materialized:$scenario]}"
done
