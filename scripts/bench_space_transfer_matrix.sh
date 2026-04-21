#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/cetta"
MODE="${1:-all}"
FACT_COUNT="${2:-10000}"
MATCH_ROUNDS="${3:-3}"
SCENARIOS_STR="${4:-copy_only exact_hit_after_copy full_scan_after_copy suite_total}"
RUNTIME_DIR="$ROOT/runtime/bench_space_transfer"

usage() {
    cat <<EOF
Usage: $(basename "$0") [all|native-to-pathmap|native-to-mork-live|pathmap-to-native|pathmap-to-mork-live|mork-live-to-native|mork-live-to-pathmap|mork-live-to-open-act|mork-live-to-load-act] [FACT_COUNT] [MATCH_ROUNDS] [SCENARIOS]
EOF
}

die() {
    printf '%s\n' "$*" >&2
    exit 1
}

case "$MODE" in
    all|native-to-pathmap|native-to-mork-live|pathmap-to-native|pathmap-to-mork-live|mork-live-to-native|mork-live-to-pathmap|mork-live-to-open-act|mork-live-to-load-act)
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
[ "$FACT_COUNT" -gt 42 ] || die "FACT_COUNT must be greater than 42 because the workload queries (friend sam 42)"
[ "$MATCH_ROUNDS" -gt 0 ] || die "MATCH_ROUNDS must be positive"
[ -x "$BIN" ] || die "Missing executable $BIN"

mkdir -p "$RUNTIME_DIR"
tmp_dir=$(mktemp -d "$RUNTIME_DIR/.bench_space_transfer.XXXXXX")
cleanup() {
    rm -rf "$tmp_dir"
}
trap cleanup EXIT

run_cetta_file() {
    local file=$1
    local output=$2
    local status=0
    /usr/bin/time -f 'time_sec=%e rss_kb=%M' \
        bash -lc "ulimit -v 10485760; ulimit -s unlimited; cd '$ROOT'; '$BIN' --quiet --lang he '$file'" \
        >"$output" 2>&1 || status=$?
    return "$status"
}

case_source_kind() {
    case "$1" in
        native-to-pathmap|native-to-mork-live) printf 'native\n' ;;
        pathmap-to-native|pathmap-to-mork-live) printf 'pathmap\n' ;;
        mork-live-to-native|mork-live-to-pathmap|mork-live-to-open-act|mork-live-to-load-act) printf 'mork-live\n' ;;
        *) die "Unknown case: $1" ;;
    esac
}

case_target_kind() {
    case "$1" in
        native-to-pathmap) printf 'pathmap\n' ;;
        native-to-mork-live|pathmap-to-mork-live) printf 'mork-live\n' ;;
        pathmap-to-native|mork-live-to-native) printf 'native\n' ;;
        mork-live-to-pathmap) printf 'pathmap\n' ;;
        mork-live-to-open-act) printf 'mork-open-act\n' ;;
        mork-live-to-load-act) printf 'mork-load-act\n' ;;
        *) die "Unknown case: $1" ;;
    esac
}

case_route_class() {
    case "$1" in
        native-to-pathmap|native-to-mork-live|pathmap-to-native|pathmap-to-mork-live|mork-live-to-native|mork-live-to-pathmap)
            printf 'materialized-shim\n'
            ;;
        mork-live-to-open-act)
            printf 'act-snapshot-open\n'
            ;;
        mork-live-to-load-act)
            printf 'act-snapshot-load\n'
            ;;
        *)
            die "Unknown case: $1"
            ;;
    esac
}

need_list_import() {
    case "$1" in
        native|pathmap|mork-live) return 0 ;;
        *) return 1 ;;
    esac
}

need_mork_import() {
    case "$1" in
        mork-live|mork-open-act|mork-load-act) return 0 ;;
        *) return 1 ;;
    esac
}

kind_bind_space() {
    local var=$1
    local kind=$2
    case "$kind" in
        native)
            printf '!(bind! %s (new-space native))\n' "$var"
            ;;
        pathmap)
            printf '!(bind! %s (new-space pathmap))\n' "$var"
            ;;
        mork-live)
            printf '!(bind! %s (mork:new-space))\n' "$var"
            ;;
        *)
            die "Unsupported bind kind: $kind"
            ;;
    esac
}

kind_populate_space() {
    local var=$1
    local kind=$2
    case "$kind" in
        native|pathmap)
            cat <<EOF
!(let \$ignored
   (collapse
     (let \$xs (eval (list:range $FACT_COUNT))
       (let \$i (superpose \$xs)
         (add-atom $var (friend sam \$i)))))
   ())
EOF
            ;;
        mork-live)
            cat <<EOF
!(let \$ignored
   (collapse
     (let \$xs (eval (list:range $FACT_COUNT))
       (let \$i (superpose \$xs)
         (mork:add-atom $var (friend sam \$i)))))
   ())
EOF
            ;;
        *)
            die "Unsupported populate kind: $kind"
            ;;
    esac
}

kind_size_expr() {
    local kind=$1
    local var=$2
    case "$kind" in
        native|pathmap)
            printf '(size %s)\n' "$var"
            ;;
        mork-live|mork-open-act|mork-load-act)
            printf '(mork:size %s)\n' "$var"
            ;;
        *)
            die "Unsupported size kind: $kind"
            ;;
    esac
}

kind_match_count_expr() {
    local kind=$1
    local var=$2
    local pattern=$3
    local template=$4
    case "$kind" in
        native|pathmap)
            printf '(size (collapse (match %s %s %s)))\n' "$var" "$pattern" "$template"
            ;;
        mork-live|mork-open-act|mork-load-act)
            printf '(size (collapse (mork:match %s %s %s)))\n' "$var" "$pattern" "$template"
            ;;
        *)
            die "Unsupported match kind: $kind"
            ;;
    esac
}

render_case_program() {
    local case_id=$1
    local scenario=$2
    local out=$3
    local src_kind
    local dst_kind
    local route_class
    local act_path="$RUNTIME_DIR/${case_id}_${FACT_COUNT}.act"
    local expected_full_scan=$((FACT_COUNT * MATCH_ROUNDS))

    src_kind="$(case_source_kind "$case_id")"
    dst_kind="$(case_target_kind "$case_id")"
    route_class="$(case_route_class "$case_id")"

    {
        if need_list_import "$src_kind"; then
            printf '!(import! &self list)\n'
        fi
        if need_mork_import "$src_kind" || need_mork_import "$dst_kind"; then
            printf '!(import! &self mork)\n'
        fi

        kind_bind_space '&src' "$src_kind"
        kind_populate_space '&src' "$src_kind"
        printf '!(assertEqualToResult %s (%s))\n' "$(kind_size_expr "$src_kind" '&src')" "$FACT_COUNT"
        printf '!(println! (bench source_rows %s))\n' "$(kind_size_expr "$src_kind" '&src')"

        case "$route_class" in
            materialized-shim)
                kind_bind_space '&dst' "$dst_kind"
                if [ "$src_kind" = "mork-live" ]; then
                    if [ "$dst_kind" = "mork-live" ]; then
                        printf '!(let $atoms (collapse (mork:get-atoms &src))\n'
                        printf '   (mork:add-atoms &dst $atoms))\n'
                    else
                        printf '!(let $atoms (collapse (mork:get-atoms &src))\n'
                        printf '   (add-atoms &dst $atoms))\n'
                    fi
                else
                    if [ "$dst_kind" = "mork-live" ]; then
                        printf '!(let $atoms (collapse (get-atoms &src))\n'
                        printf '   (mork:add-atoms &dst $atoms))\n'
                    else
                        printf '!(let $atoms (collapse (get-atoms &src))\n'
                        printf '   (add-atoms &dst $atoms))\n'
                    fi
                fi
                ;;
            act-snapshot-open)
                printf '!(assertEqual (mork:dump! &src "%s") ())\n' "$act_path"
                printf '!(bind! &dst (mork:open-act "%s"))\n' "$act_path"
                ;;
            act-snapshot-load)
                printf '!(assertEqual (mork:dump! &src "%s") ())\n' "$act_path"
                printf '!(bind! &dst (mork:new-space))\n'
                printf '!(mork:load-act! &dst "%s")\n' "$act_path"
                ;;
            *)
                die "Unknown route class: $route_class"
                ;;
        esac

        printf '!(assertEqualToResult %s (%s))\n' "$(kind_size_expr "$dst_kind" '&dst')" "$FACT_COUNT"

        case "$scenario" in
            copy_only)
                printf '!(println! (bench scenario_rows %s))\n' "$(kind_size_expr "$dst_kind" '&dst')"
                ;;
            exact_hit_after_copy)
                printf '!(let $rows %s\n' "$(kind_match_count_expr "$dst_kind" '&dst' '(friend sam 42)' '(friend sam 42)')"
                printf '   (println! (bench scenario_rows $rows)))\n'
                printf '!(assertEqualToResult %s (1))\n' "$(kind_match_count_expr "$dst_kind" '&dst' '(friend sam 42)' '(friend sam 42)')"
                ;;
            full_scan_after_copy)
                printf '!(bind! &acc (new-state 0))\n'
                for _ in $(seq 1 "$MATCH_ROUNDS"); do
                    printf '!(let $rows %s\n' "$(kind_match_count_expr "$dst_kind" '&dst' '(friend $y $x)' '(friend $y $x)')"
                    printf '   (change-state! &acc (+ (get-state &acc) $rows)))\n'
                done
                printf '!(println! (bench scenario_rows (get-state &acc)))\n'
                printf '!(assertEqualToResult (get-state &acc) (%s))\n' "$expected_full_scan"
                ;;
            suite_total)
                printf '!(assertEqualToResult %s (1))\n' "$(kind_match_count_expr "$dst_kind" '&dst' '(friend sam 42)' '(friend sam 42)')"
                printf '!(bind! &acc (new-state 0))\n'
                for _ in $(seq 1 "$MATCH_ROUNDS"); do
                    printf '!(let $rows %s\n' "$(kind_match_count_expr "$dst_kind" '&dst' '(friend $y $x)' '(friend $y $x)')"
                    printf '   (change-state! &acc (+ (get-state &acc) $rows)))\n'
                done
                printf '!(println! (bench scenario_rows (get-state &acc)))\n'
                printf '!(assertEqualToResult (get-state &acc) (%s))\n' "$expected_full_scan"
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

run_case_scenario() {
    local case_id=$1
    local scenario=$2
    local src_kind
    local dst_kind
    local route_class
    local file="$tmp_dir/${case_id}_${scenario}_${FACT_COUNT}.metta"
    local log="$tmp_dir/${case_id}_${scenario}_${FACT_COUNT}.log"
    local source_rows
    local scenario_rows
    local time_sec
    local rss_kb
    local expected

    src_kind="$(case_source_kind "$case_id")"
    dst_kind="$(case_target_kind "$case_id")"
    route_class="$(case_route_class "$case_id")"

    render_case_program "$case_id" "$scenario" "$file"
    run_cetta_file "$file" "$log"

    source_rows=$(extract_metric source_rows "$log")
    scenario_rows=$(extract_metric scenario_rows "$log")
    time_sec=$(extract_time "$log")
    rss_kb=$(extract_rss "$log")

    case "$scenario" in
        copy_only) expected="$FACT_COUNT" ;;
        exact_hit_after_copy) expected="1" ;;
        full_scan_after_copy|suite_total) expected="$((FACT_COUNT * MATCH_ROUNDS))" ;;
        *) die "Unknown scenario: $scenario" ;;
    esac

    expect_value "$case_id/$scenario source_rows" "$FACT_COUNT" "$source_rows"
    expect_value "$case_id/$scenario scenario_rows" "$expected" "$scenario_rows"
    [ -n "$time_sec" ] || die "missing timing for $case_id/$scenario"
    [ -n "$rss_kb" ] || die "missing RSS for $case_id/$scenario"

    printf 'transfer\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$case_id" "$src_kind" "$dst_kind" "$route_class" "$scenario" "$FACT_COUNT" "$MATCH_ROUNDS" "$scenario_rows" "$time_sec" "$rss_kb"
}

IFS=' ' read -r -a scenarios <<< "$SCENARIOS_STR"
selected_cases=()
case "$MODE" in
    all)
        selected_cases=(
            native-to-pathmap
            native-to-mork-live
            pathmap-to-native
            pathmap-to-mork-live
            mork-live-to-native
            mork-live-to-pathmap
            mork-live-to-open-act
            mork-live-to-load-act
        )
        ;;
    *)
        selected_cases=("$MODE")
        ;;
esac

printf 'kind\tcase_id\tsource_kind\ttarget_kind\troute_class\tscenario\tfact_count\tmatch_rounds\tcount\ttime_s\trss_kb\n'
for case_id in "${selected_cases[@]}"; do
    for scenario in "${scenarios[@]}"; do
        run_case_scenario "$case_id" "$scenario"
    done
done
