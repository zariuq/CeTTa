#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "$0")/.." && pwd)
CETTA_BIN="$ROOT/cetta"
MODE=${1:-all}
FACT_COUNT=${2:-100000}
MATCH_ROUNDS=${3:-3}
ACT_PATH="$ROOT/runtime/bench_mork_friend_${FACT_COUNT}.act"
TMP_DIR=$(mktemp -d "$ROOT/runtime/.bench_mork_friend.XXXXXX")

cleanup() {
    rm -rf "$TMP_DIR"
}
trap cleanup EXIT

usage() {
    cat <<EOF
Usage: $(basename "$0") [prepare|live|open-act|load-act|all|bounded-live|bounded-open-act|bounded-load-act|bounded-all] [FACT_COUNT] [MATCH_ROUNDS]

Examples:
  ./scripts/bench_mork_friend_workload.sh all 100000 3
  ./scripts/bench_mork_friend_workload.sh live 10000 3
  ./scripts/bench_mork_friend_workload.sh open-act 100000 1
  ./scripts/bench_mork_friend_workload.sh bounded-open-act 50000
EOF
}

die() {
    printf '%s\n' "$*" >&2
    exit 1
}

case "$MODE" in
    prepare|live|open-act|load-act|all|bounded-live|bounded-open-act|bounded-load-act|bounded-all)
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
[ -x "$CETTA_BIN" ] || die "Missing executable $CETTA_BIN"

run_cetta_file() {
    local file=$1
    local output=$2
    local status=0
    /usr/bin/time -f 'elapsed=%e rss=%MKB' \
        bash -lc "ulimit -v 10485760; ulimit -s unlimited; '$CETTA_BIN' --quiet --lang he '$file'" \
        >"$output" 2>&1 || status=$?
    return "$status"
}

render_prepare_file() {
    local file=$1
    cat >"$file" <<EOF
!(import! &self list)
!(import! &self mork)
!(bind! &s (mork:new-space))
!(let \$xs (eval (list:range $FACT_COUNT))
   (let \$i (superpose \$xs)
      (mork:add-atom &s (friend sam \$i))))
!(assertEqualToResult (mork:size &s) ($FACT_COUNT))
!(mork:dump! &s "$ACT_PATH")
!(println! (bench prepared_rows $FACT_COUNT))
EOF
}

render_case_file() {
    local mode=$1
    local file=$2
    local expected_full_scan=$((FACT_COUNT * MATCH_ROUNDS))
    local expected_remaining=$((FACT_COUNT - 1))

    cat >"$file" <<EOF
!(import! &self mork)
EOF

    if [ "$mode" = "live" ]; then
        cat >>"$file" <<EOF
!(import! &self list)
!(bind! &s (mork:new-space))
!(let \$xs (eval (list:range $FACT_COUNT))
   (let \$i (superpose \$xs)
      (mork:add-atom &s (friend sam \$i))))
EOF
    elif [ "$mode" = "open-act" ]; then
        cat >>"$file" <<EOF
!(bind! &s (mork:open-act "$ACT_PATH"))
EOF
    else
        cat >>"$file" <<EOF
!(bind! &s (mork:new-space))
!(mork:load-act! &s "$ACT_PATH")
EOF
    fi

    cat >>"$file" <<EOF
!(assertEqualToResult (mork:size &s) ($FACT_COUNT))
!(println! (bench initial_size_rows $FACT_COUNT))

!(let \$rows (collect (mork:match &s (friend sam 42) hit))
   (println! (bench exact_hit_rows (size-atom \$rows))))

!(let \$rows (collect (mork:match &s (friend sam $FACT_COUNT) hit))
   (println! (bench exact_miss_rows (size-atom \$rows))))

!(bind! &full_scan_total (new-state 0))
EOF

    local round
    for round in $(seq 1 "$MATCH_ROUNDS"); do
        cat >>"$file" <<EOF
!(let \$rows (collect (mork:match &s (friend \$y \$x) hit))
   (change-state! &full_scan_total
     (eval (+ (get-state &full_scan_total) (size-atom \$rows)))))
EOF
    done

    cat >>"$file" <<EOF
!(println! (bench full_scan_total_rows (get-state &full_scan_total)))

!(mork:remove-atom &s (friend sam 42))
!(println! (bench remove_rows 1))

!(let \$rows (collect (mork:match &s (friend sam 42) hit))
   (println! (bench post_remove_exact_rows (size-atom \$rows))))

!(let \$rows (collect (mork:match &s (friend \$y \$x) hit))
   (println! (bench post_remove_count_rows (size-atom \$rows))))

!(assertEqualToResult (mork:size &s) ($expected_remaining))
!(let \$rows (collect (mork:match &s (friend sam 42) hit))
   (assertEqualToResult (size-atom \$rows) (0)))
!(let \$rows (collect (mork:match &s (friend \$y \$x) hit))
   (assertEqualToResult (size-atom \$rows) ($expected_remaining)))
EOF
}

render_bounded_case_file() {
    local mode=$1
    local variant=$2
    local file=$3

    cat >"$file" <<EOF
!(import! &self mork)
EOF

    if [ "$mode" = "live" ]; then
        cat >>"$file" <<EOF
!(import! &self list)
!(bind! &s (mork:new-space))
!(let \$xs (eval (list:range $FACT_COUNT))
   (let \$i (superpose \$xs)
      (mork:add-atom &s (friend sam \$i))))
EOF
    elif [ "$mode" = "open-act" ]; then
        cat >>"$file" <<EOF
!(bind! &s (mork:open-act "$ACT_PATH"))
EOF
    else
        cat >>"$file" <<EOF
!(bind! &s (mork:new-space))
!(mork:load-act! &s "$ACT_PATH")
EOF
    fi

    cat >>"$file" <<EOF
!(assertEqualToResult (mork:size &s) ($FACT_COUNT))
!(println! (bench initial_size_rows $FACT_COUNT))
EOF

    case "$variant" in
        select1)
            cat >>"$file" <<EOF
!(assertEqualToResult (select 1 (mork:match &s (friend \$y \$x) hit)) (hit))
!(println! (bench demand_rows 1))
EOF
            ;;
        select5)
            cat >>"$file" <<EOF
!(let \$rows (select 5 (mork:match &s (friend \$y \$x) hit))
   (println! (bench demand_rows (size-atom \$rows))))
EOF
            ;;
        collect)
            cat >>"$file" <<EOF
!(let \$rows (collect (mork:match &s (friend \$y \$x) hit))
   (println! (bench demand_rows (size-atom \$rows))))
EOF
            ;;
        *)
            die "Unknown bounded variant: $variant"
            ;;
    esac
}

parse_metric() {
    local key=$1
    local file=$2
    awk -v want="$key" '
        /^\(bench / {
            gsub(/[()]/, "", $0);
            if ($2 == want) {
                print $3;
                exit;
            }
        }
    ' "$file"
}

require_metric() {
    local key=$1
    local expected=$2
    local file=$3
    local actual
    actual=$(parse_metric "$key" "$file")
    if [ -z "$actual" ]; then
        printf 'Missing metric %s\n' "$key" >&2
        cat "$file" >&2
        exit 1
    fi
    if [ "$actual" != "$expected" ]; then
        printf 'Metric %s expected %s, got %s\n' "$key" "$expected" "$actual" >&2
        cat "$file" >&2
        exit 1
    fi
}

prepare_act() {
    local file="$TMP_DIR/prepare.metta"
    local log="$TMP_DIR/prepare.log"
    render_prepare_file "$file"
    run_cetta_file "$file" "$log"
    require_metric prepared_rows "$FACT_COUNT" "$log"
    printf 'prepared ACT: %s\n' "$ACT_PATH"
}

run_case() {
    local mode=$1
    local file="$TMP_DIR/$mode.metta"
    local log="$TMP_DIR/$mode.log"
    local expected_full_scan=$((FACT_COUNT * MATCH_ROUNDS))
    local expected_remaining=$((FACT_COUNT - 1))
    local timing

    render_case_file "$mode" "$file"
    run_cetta_file "$file" "$log"

    require_metric initial_size_rows "$FACT_COUNT" "$log"
    require_metric exact_hit_rows 1 "$log"
    require_metric exact_miss_rows 0 "$log"
    require_metric full_scan_total_rows "$expected_full_scan" "$log"
    require_metric remove_rows 1 "$log"
    require_metric post_remove_exact_rows 0 "$log"
    require_metric post_remove_count_rows "$expected_remaining" "$log"

    timing=$(grep 'elapsed=' "$log" | tail -1 || true)
    printf '%-8s initial=%s exact_hit=%s exact_miss=%s full_scan=%s post_remove=%s %s\n' \
        "$mode" \
        "$(parse_metric initial_size_rows "$log")" \
        "$(parse_metric exact_hit_rows "$log")" \
        "$(parse_metric exact_miss_rows "$log")" \
        "$(parse_metric full_scan_total_rows "$log")" \
        "$(parse_metric post_remove_count_rows "$log")" \
        "$timing"
}

run_bounded_case() {
    local mode=$1
    local variant=$2
    local file="$TMP_DIR/${mode}_${variant}.metta"
    local log="$TMP_DIR/${mode}_${variant}.log"
    local expected_rows
    local timing

    case "$variant" in
        select1)
            expected_rows=1
            ;;
        select5)
            expected_rows=5
            ;;
        collect)
            expected_rows=$FACT_COUNT
            ;;
        *)
            die "Unknown bounded variant: $variant"
            ;;
    esac

    render_bounded_case_file "$mode" "$variant" "$file"
    run_cetta_file "$file" "$log"

    require_metric initial_size_rows "$FACT_COUNT" "$log"
    require_metric demand_rows "$expected_rows" "$log"

    timing=$(grep 'elapsed=' "$log" | tail -1 || true)
    printf '%-8s %-7s rows=%s %s\n' \
        "$mode" \
        "$variant" \
        "$(parse_metric demand_rows "$log")" \
        "$timing"
}

if [ "$MODE" = "prepare" ]; then
    prepare_act
    exit 0
fi

if [ "$MODE" = "open-act" ] || [ "$MODE" = "load-act" ] || [ "$MODE" = "all" ] || \
   [ "$MODE" = "bounded-open-act" ] || [ "$MODE" = "bounded-load-act" ] || \
   [ "$MODE" = "bounded-all" ]; then
    if [ ! -f "$ACT_PATH" ]; then
        prepare_act
    fi
fi

case "$MODE" in
    live)
        run_case live
        ;;
    open-act)
        run_case open-act
        ;;
    load-act)
        run_case load-act
        ;;
    bounded-live)
        run_bounded_case live select1
        run_bounded_case live select5
        run_bounded_case live collect
        ;;
    bounded-open-act)
        run_bounded_case open-act select1
        run_bounded_case open-act select5
        run_bounded_case open-act collect
        ;;
    bounded-load-act)
        run_bounded_case load-act select1
        run_bounded_case load-act select5
        run_bounded_case load-act collect
        ;;
    all)
        prepare_act
        run_case live
        run_case open-act
        run_case load-act
        ;;
    bounded-all)
        prepare_act
        run_bounded_case live select1
        run_bounded_case live select5
        run_bounded_case live collect
        run_bounded_case open-act select1
        run_bounded_case open-act select5
        run_bounded_case open-act collect
        run_bounded_case load-act select1
        run_bounded_case load-act select5
        run_bounded_case load-act collect
        ;;
esac
