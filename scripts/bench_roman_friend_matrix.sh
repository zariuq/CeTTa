#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
CETTA_BIN="${CETTA_BIN:-$ROOT/cetta}"
PETTA_DIR="${PETTA_DIR:-$ROOT/../PeTTa}"
PETTA_HE_DIR="${PETTA_HE_DIR:-$ROOT/../petta-he-profile}"
TRANSLATOR="${TRANSLATOR:-$ROOT/../translators/translate.sh}"
FACT_COUNT="${1:-1000}"
MATCH_ROUNDS="${2:-2}"
LANES_STR="${BENCH_ROMAN_LANES:-cetta-mork cetta-pathmap cetta-native cetta-native-to-mork cetta-pathmap-to-mork petta-mork petta-pure}"
TIMEOUT_SECONDS="${BENCH_ROMAN_TIMEOUT_SECONDS:-600}"
RUNTIME_DIR="$ROOT/runtime/bench_roman_friend_matrix"
KEEP_TMP="${BENCH_KEEP_TMP:-0}"

usage() {
    cat <<EOF
Usage: $(basename "$0") [FACT_COUNT] [MATCH_ROUNDS]

Environment:
  BENCH_ROMAN_LANES              space-separated lanes
                                 default: cetta-mork cetta-pathmap cetta-native cetta-native-to-mork cetta-pathmap-to-mork petta-mork petta-pure
  BENCH_ROMAN_TIMEOUT_SECONDS    per-lane timeout, default 600
  PETTA_DIR                      PeTTa checkout, default ../PeTTa
  PETTA_HE_DIR                   PeTTa --he checkout, default ../petta-he-profile
  TRANSLATOR                     PeTTa->HE translator, default ../translators/translate.sh
  BENCH_KEEP_TMP=1               preserve generated programs/logs

Lanes:
  cetta-mork       CeTTa explicit mork:* over an explicit MorkSpace
  cetta-pathmap    CeTTa ordinary PathMap space through add-atom/match
  cetta-native     CeTTa ordinary native space through add-atom/match
  cetta-native-to-mork
                   CeTTa native source space, then materialized load to mork:*
  cetta-pathmap-to-mork
                   CeTTa PathMap source space, then bridge-row transfer to mork:*
  petta-mork       PeTTa &mork over MORK FFI
  cetta-mork-batch CeTTa explicit mork:* with batch insert (opt-in)
  petta-pure       PeTTa ordinary space baseline
  petta-he-mork    PeTTa->HE translated petta-mork program under PeTTa --he (opt-in)
EOF
}

die() {
    printf '%s\n' "$*" >&2
    exit 1
}

case "${1:-}" in
    -h|--help)
        usage
        exit 0
        ;;
esac

[[ "$FACT_COUNT" =~ ^[0-9]+$ ]] || die "FACT_COUNT must be a non-negative integer"
[[ "$MATCH_ROUNDS" =~ ^[0-9]+$ ]] || die "MATCH_ROUNDS must be a non-negative integer"
[ "$FACT_COUNT" -gt 42 ] || die "FACT_COUNT must be greater than 42 because the workload removes (friend sam 42)"
[ "$MATCH_ROUNDS" -gt 0 ] || die "MATCH_ROUNDS must be positive"
[ -x "$CETTA_BIN" ] || die "Missing executable $CETTA_BIN"

IFS=' ' read -r -a LANES <<< "$LANES_STR"

cetta_lane_needs_bridge() {
    case "$1" in
        cetta-mork|cetta-mork-batch|cetta-pathmap|cetta-native-to-mork|cetta-pathmap-to-mork)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

cetta_lanes_need_bridge() {
    local lane
    for lane in "${LANES[@]}"; do
        if cetta_lane_needs_bridge "$lane"; then
            return 0
        fi
    done
    return 1
}

mkdir -p "$RUNTIME_DIR"
tmp_dir="$(mktemp -d "$RUNTIME_DIR/.bench_roman_friend.XXXXXX")"
cleanup() {
    if [ "$KEEP_TMP" = "1" ]; then
        printf 'preserved_tmp\t%s\n' "$tmp_dir" >&2
        return
    fi
    rm -rf "$tmp_dir"
}
trap cleanup EXIT

preflight_cetta_bridge() {
    local probe="$tmp_dir/cetta-bridge-preflight.metta"
    local log="$tmp_dir/cetta-bridge-preflight.log"
    cat >"$probe" <<'EOF'
!(import! &self mork)
!(bind! &m (mork:new-space))
!(mork:add-atom &m (probe ok))
!(assertEqualToResult (mork:size &m) (1))
!(bind! &p (new-space pathmap))
!(add-atom &p (probe ok))
!(assertEqualToResult (size &p) (1))
EOF
    if ! "$CETTA_BIN" --quiet --profile he-extended --lang he "$probe" >"$log" 2>&1 ||
       grep -q '(Error ' "$log"; then
        printf '%s\n' "CeTTa MORK/PathMap lanes require a working bridge build. Run 'make BUILD=mork' or provide a working CETTA_MORK_SPACE_BRIDGE_LIB." >&2
        sed -n '1,80p' "$log" >&2
        exit 1
    fi
}

if cetta_lanes_need_bridge; then
    preflight_cetta_bridge
fi

seconds_from_ns_expr() {
    printf '(/ (- (get-state &t1) (get-state &t0)) 1000000000.0)'
}

emit_cetta_query_phases() {
    local match_expr=$1
    local remove_expr=$2
    local size_expr=$3
    local expected_remaining=$4
    local round

    printf '!(assertEqualToResult (%s) (%s))\n' "$size_expr" "$FACT_COUNT"

    printf '!(change-state! &t0 (system:monotonic-ns))\n'
    printf '!(change-state! &scan-total\n'
    printf '   (size (collapse (%s (friend sam 42) hit))))\n' "$match_expr"
    printf '!(change-state! &t1 (system:monotonic-ns))\n'
    printf '!(println! (bench exact_hit_rows (get-state &scan-total)))\n'
    printf '!(println! (bench exact_hit_seconds %s))\n' "$(seconds_from_ns_expr)"

    printf '!(change-state! &t0 (system:monotonic-ns))\n'
    printf '!(change-state! &scan-total\n'
    printf '   (size (collapse (%s (friend sam %s) hit))))\n' "$match_expr" "$FACT_COUNT"
    printf '!(change-state! &t1 (system:monotonic-ns))\n'
    printf '!(println! (bench exact_miss_rows (get-state &scan-total)))\n'
    printf '!(println! (bench exact_miss_seconds %s))\n' "$(seconds_from_ns_expr)"

    printf '!(change-state! &scan-total 0)\n'
    printf '!(change-state! &t0 (system:monotonic-ns))\n'
    for round in $(seq 1 "$MATCH_ROUNDS"); do
        printf '!(change-state! &scan-total\n'
        printf '   (+ (get-state &scan-total)\n'
        printf '      (size (collapse (%s (friend $y $x) hit)))))\n' "$match_expr"
    done
    printf '!(change-state! &t1 (system:monotonic-ns))\n'
    printf '!(println! (bench full_scan_total_rows (get-state &scan-total)))\n'
    printf '!(println! (bench full_scan_seconds %s))\n' "$(seconds_from_ns_expr)"

    printf '!(change-state! &t0 (system:monotonic-ns))\n'
    printf '!(%s (friend sam 42))\n' "$remove_expr"
    printf '!(change-state! &t1 (system:monotonic-ns))\n'
    printf '!(println! (bench remove_rows 1))\n'
    printf '!(println! (bench remove_seconds %s))\n' "$(seconds_from_ns_expr)"

    printf '!(change-state! &t0 (system:monotonic-ns))\n'
    printf '!(change-state! &scan-total\n'
    printf '   (size (collapse (%s (friend sam 42) hit))))\n' "$match_expr"
    printf '!(change-state! &t1 (system:monotonic-ns))\n'
    printf '!(println! (bench post_remove_exact_rows (get-state &scan-total)))\n'
    printf '!(println! (bench post_remove_exact_seconds %s))\n' "$(seconds_from_ns_expr)"

    printf '!(change-state! &t0 (system:monotonic-ns))\n'
    printf '!(change-state! &scan-total\n'
    printf '   (size (collapse (%s (friend $y $x) hit))))\n' "$match_expr"
    printf '!(change-state! &t1 (system:monotonic-ns))\n'
    printf '!(println! (bench post_remove_count_rows (get-state &scan-total)))\n'
    printf '!(println! (bench post_remove_count_seconds %s))\n' "$(seconds_from_ns_expr)"

    printf '!(change-state! &total-end (system:monotonic-ns))\n'
    printf '!(println! (bench total_seconds (/ (- (get-state &total-end) (get-state &total-start)) 1000000000.0)))\n'
    printf '!(assertEqualToResult (%s) (%s))\n' "$size_expr" "$expected_remaining"
    printf '!(assertEqualToResult\n'
    printf '   (size (collapse (%s (friend sam 42) hit)))\n' "$match_expr"
    printf '   (0))\n'
    printf '!(assertEqualToResult\n'
    printf '   (size (collapse (%s (friend $y $x) hit)))\n' "$match_expr"
    printf '   (%s))\n' "$expected_remaining"
    printf '!(assertEqualToResult (get-state &scan-total) (%s))\n' "$expected_remaining"
}

render_cetta_case() {
    local lane=$1
    local output=$2
    local backend=$3
    local add_expr match_expr remove_expr size_expr
    local expected_full_scan=$((FACT_COUNT * MATCH_ROUNDS))
    local expected_remaining=$((FACT_COUNT - 1))
    local round

    if [ "$backend" = "mork" ] || [ "$backend" = "mork-batch" ]; then
        add_expr='mork:add-atom &bench-space'
        match_expr='mork:match &bench-space'
        remove_expr='mork:remove-atom &bench-space'
        size_expr='mork:size &bench-space'
    else
        add_expr='add-atom &bench-space'
        match_expr='match &bench-space'
        remove_expr='remove-atom &bench-space'
        size_expr='size &bench-space'
    fi

    {
        printf '!(import! &self system)\n'
        printf '!(import! &self list)\n'
        if [ "$backend" = "mork" ] || [ "$backend" = "mork-batch" ]; then
            printf '!(import! &self mork)\n'
            printf '!(bind! &bench-space (mork:new-space))\n'
        elif [ "$backend" = "pathmap" ]; then
            printf '!(bind! &bench-space (new-space pathmap))\n'
        else
            printf '!(bind! &bench-space (new-space))\n'
        fi
        cat <<'EOF'
!(bind! &t0 (new-state 0))
!(bind! &t1 (new-state 0))
!(bind! &scan-total (new-state 0))
!(bind! &total-start (new-state 0))
!(bind! &total-end (new-state 0))
EOF
        printf '!(change-state! &total-start (system:monotonic-ns))\n'
        printf '!(change-state! &t0 (system:monotonic-ns))\n'
        if [ "$backend" = "mork-batch" ]; then
            printf '!(let $atoms (collapse (let $xs (eval (list:range %s)) (let $x (superpose $xs) (friend sam $x))))\n' "$FACT_COUNT"
            printf '   (mork:add-atoms &bench-space $atoms))\n'
        else
            printf '!(let $ignored (collapse (let $xs (eval (list:range %s)) (let $x (superpose $xs) (%s (friend sam $x))))) ())\n' "$FACT_COUNT" "$add_expr"
        fi
        printf '!(change-state! &t1 (system:monotonic-ns))\n'
        printf '!(println! (bench insert_rows %s))\n' "$FACT_COUNT"
        if [ "$backend" = "mork" ] || [ "$backend" = "mork-batch" ]; then
            printf '!(println! (bench source_insert_seconds 0))\n'
            printf '!(println! (bench bridge_load_seconds %s))\n' "$(seconds_from_ns_expr)"
        else
            printf '!(println! (bench source_insert_seconds %s))\n' "$(seconds_from_ns_expr)"
            printf '!(println! (bench bridge_load_seconds 0))\n'
        fi
        printf '!(println! (bench insert_seconds %s))\n' "$(seconds_from_ns_expr)"
        emit_cetta_query_phases "$match_expr" "$remove_expr" "$size_expr" "$expected_remaining"
        printf '; lane %s expected full scan rows %s\n' "$lane" "$expected_full_scan"
    } >"$output"
}

render_cetta_space_to_mork_case() {
    local lane=$1
    local output=$2
    local source_backend=$3
    local expected_full_scan=$((FACT_COUNT * MATCH_ROUNDS))
    local expected_remaining=$((FACT_COUNT - 1))
    local source_ctor="new-space native"
    local load_expr='(let $ignored (collapse (let $atom (get-atoms &source-space) (mork:add-atom &bench-space $atom))) ())'

    if [ "$source_backend" = "pathmap" ]; then
        source_ctor="new-space pathmap"
        load_expr='(mork:add-atoms &bench-space (get-atoms &source-space))'
    fi

    {
        printf '!(import! &self system)\n'
        printf '!(import! &self list)\n'
        printf '!(import! &self mork)\n'
        printf '!(bind! &source-space (%s))\n' "$source_ctor"
        printf '!(bind! &bench-space (mork:new-space))\n'
        cat <<'EOF'
!(bind! &t0 (new-state 0))
!(bind! &t1 (new-state 0))
!(bind! &scan-total (new-state 0))
!(bind! &source-seconds (new-state 0))
!(bind! &load-seconds (new-state 0))
!(bind! &total-start (new-state 0))
!(bind! &total-end (new-state 0))
EOF
        printf '!(change-state! &total-start (system:monotonic-ns))\n'
        printf '!(change-state! &t0 (system:monotonic-ns))\n'
        printf '!(let $ignored (collapse (let $xs (eval (list:range %s)) (let $x (superpose $xs) (add-atom &source-space (friend sam $x))))) ())\n' "$FACT_COUNT"
        printf '!(change-state! &t1 (system:monotonic-ns))\n'
        printf '!(change-state! &source-seconds %s)\n' "$(seconds_from_ns_expr)"
        printf '!(println! (bench source_insert_seconds (get-state &source-seconds)))\n'
        printf '!(assertEqualToResult (size &source-space) (%s))\n' "$FACT_COUNT"

        printf '!(change-state! &t0 (system:monotonic-ns))\n'
        printf '!%s\n' "$load_expr"
        printf '!(change-state! &t1 (system:monotonic-ns))\n'
        printf '!(change-state! &load-seconds %s)\n' "$(seconds_from_ns_expr)"
        printf '!(println! (bench bridge_load_seconds (get-state &load-seconds)))\n'
        printf '!(println! (bench insert_rows %s))\n' "$FACT_COUNT"
        printf '!(println! (bench insert_seconds (+ (get-state &source-seconds) (get-state &load-seconds))))\n'
        emit_cetta_query_phases "mork:match &bench-space" "mork:remove-atom &bench-space" "mork:size &bench-space" "$expected_remaining"
        printf '; lane %s expected full scan rows %s\n' "$lane" "$expected_full_scan"
    } >"$output"
}

render_petta_case() {
    local output=$1
    local space_name=$2
    local needs_mork=$3
    local expected_remaining=$((FACT_COUNT - 1))
    local round

    {
        if [ "$needs_mork" = "1" ]; then
            printf '!(mm2-exec &mork 1)\n'
        else
            printf '!(bind! %s (new-space))\n' "$space_name"
        fi
        cat <<'EOF'
!(bind! t0 (new-state 0))
!(bind! t1 (new-state 0))
!(bind! metric (new-state 0))
!(bind! scan_rows_total (new-state 0))
!(bind! scan_secs_total (new-state 0))
!(bind! total_start (new-state 0))
!(bind! total_end (new-state 0))

(= (bench-range $k $n)
   (if (< $k $n)
       (superpose ($k (bench-range (+ $k 1) $n)))
       (empty)))
EOF
        printf '!(change-state! total_start (current-time))\n'
        printf '!(change-state! t0 (current-time))\n'
        printf '!(let $ignored (collapse (let $x (bench-range 0 %s) (add-atom %s (friend sam $x)))) ())\n' "$FACT_COUNT" "$space_name"
        printf '!(change-state! t1 (current-time))\n'
        printf '!(println! (bench insert_rows %s))\n' "$FACT_COUNT"
        printf '!(change-state! metric (- (get-state t1) (get-state t0)))\n'
        if [ "$needs_mork" = "1" ]; then
            printf '!(println! (bench source_insert_seconds 0))\n'
            printf '!(println! (bench bridge_load_seconds (get-state metric)))\n'
        else
            printf '!(println! (bench source_insert_seconds (get-state metric)))\n'
            printf '!(println! (bench bridge_load_seconds 0))\n'
        fi
        printf '!(println! (bench insert_seconds (get-state metric)))\n'

        printf '!(change-state! t0 (current-time))\n'
        printf '!(change-state! metric (length (collapse (match %s (friend sam 42) hit))))\n' "$space_name"
        printf '!(change-state! t1 (current-time))\n'
        printf '!(println! (bench exact_hit_rows (get-state metric)))\n'
        printf '!(println! (bench exact_hit_seconds (- (get-state t1) (get-state t0))))\n'

        printf '!(change-state! t0 (current-time))\n'
        printf '!(change-state! metric (length (collapse (match %s (friend sam %s) hit))))\n' "$space_name" "$FACT_COUNT"
        printf '!(change-state! t1 (current-time))\n'
        printf '!(println! (bench exact_miss_rows (get-state metric)))\n'
        printf '!(println! (bench exact_miss_seconds (- (get-state t1) (get-state t0))))\n'

        printf '!(change-state! scan_rows_total 0)\n'
        printf '!(change-state! scan_secs_total 0)\n'
        for round in $(seq 1 "$MATCH_ROUNDS"); do
            printf '!(change-state! t0 (current-time))\n'
            printf '!(change-state! metric (length (collapse (match %s (friend $y $x) hit))))\n' "$space_name"
            printf '!(change-state! t1 (current-time))\n'
            printf '!(change-state! scan_rows_total (+ (get-state scan_rows_total) (get-state metric)))\n'
            printf '!(change-state! scan_secs_total (+ (get-state scan_secs_total) (- (get-state t1) (get-state t0))))\n'
        done
        printf '!(println! (bench full_scan_total_rows (get-state scan_rows_total)))\n'
        printf '!(println! (bench full_scan_seconds (get-state scan_secs_total)))\n'

        printf '!(change-state! t0 (current-time))\n'
        printf '!(remove-atom %s (friend sam 42))\n' "$space_name"
        printf '!(change-state! t1 (current-time))\n'
        printf '!(println! (bench remove_rows 1))\n'
        printf '!(println! (bench remove_seconds (- (get-state t1) (get-state t0))))\n'

        printf '!(change-state! t0 (current-time))\n'
        printf '!(change-state! metric (length (collapse (match %s (friend sam 42) hit))))\n' "$space_name"
        printf '!(change-state! t1 (current-time))\n'
        printf '!(println! (bench post_remove_exact_rows (get-state metric)))\n'
        printf '!(println! (bench post_remove_exact_seconds (- (get-state t1) (get-state t0))))\n'

        printf '!(change-state! t0 (current-time))\n'
        printf '!(change-state! metric (length (collapse (match %s (friend $y $x) hit))))\n' "$space_name"
        printf '!(change-state! t1 (current-time))\n'
        printf '!(println! (bench post_remove_count_rows (get-state metric)))\n'
        printf '!(println! (bench post_remove_count_seconds (- (get-state t1) (get-state t0))))\n'

        printf '!(change-state! total_end (current-time))\n'
        printf '!(println! (bench total_seconds (- (get-state total_end) (get-state total_start))))\n'
        printf '!(test (length (collapse (match %s (friend sam 42) hit))) 0)\n' "$space_name"
        printf '!(test (length (collapse (match %s (friend $y $x) hit))) %s)\n' "$space_name" "$expected_remaining"
    } >"$output"
}

run_with_time() {
    local lane=$1
    local log=$2
    shift 2
    local status=0
    timeout "$TIMEOUT_SECONDS" /usr/bin/time -f '__wall_seconds=%e __rss_kb=%M' "$@" >"$log" 2>&1 || status=$?
    return "$status"
}

metric_value() {
    local key=$1
    local file=$2
    awk -v want="$key" '
        {
            line = $0
            gsub(/^\[/, "", line)
            gsub(/\]$/, "", line)
            gsub(/[()]/, "", line)
            if ($0 ~ /\(bench / || line ~ /^bench /) {
                n = split(line, fields, /[[:space:]]+/)
                for (i = 1; i <= n - 2; i++) {
                    if (fields[i] == "bench" && fields[i + 1] == want) {
                        print fields[i + 2]
                        exit
                    }
                }
            }
        }
    ' "$file"
}

time_metric() {
    local key=$1
    local file=$2
    awk -v want="$key" '
        {
            for (i = 1; i <= NF; i++) {
                split($i, kv, "=")
                if (kv[1] == want) {
                    print kv[2]
                    exit
                }
            }
        }
    ' "$file"
}

print_result() {
    local lane=$1
    local engine=$2
    local backend=$3
    local source=$4
    local status=$5
    local log=$6
    local expected_full_scan=$((FACT_COUNT * MATCH_ROUNDS))
    local expected_remaining=$((FACT_COUNT - 1))
    local keys=(
        insert_rows insert_seconds source_insert_seconds bridge_load_seconds
        exact_hit_rows exact_hit_seconds
        exact_miss_rows exact_miss_seconds
        full_scan_total_rows full_scan_seconds
        remove_rows remove_seconds
        post_remove_exact_rows post_remove_exact_seconds
        post_remove_count_rows post_remove_count_seconds
        total_seconds
    )
    local values=()
    local missing=()
    local key value wall rss ok

    ok=1
    for key in "${keys[@]}"; do
        value="$(metric_value "$key" "$log")"
        if [ -z "$value" ]; then
            ok=0
            missing+=("$key")
        fi
        values+=("${value:-NA}")
    done
    wall="$(time_metric __wall_seconds "$log")"
    rss="$(time_metric __rss_kb "$log")"
    [ -n "$wall" ] || wall="NA"
    [ -n "$rss" ] || rss="NA"

    [ "${values[0]}" = "$FACT_COUNT" ] || ok=0
    [ "${values[4]}" = "1" ] || ok=0
    [ "${values[6]}" = "0" ] || ok=0
    [ "${values[8]}" = "$expected_full_scan" ] || ok=0
    [ "${values[10]}" = "1" ] || ok=0
    [ "${values[12]}" = "0" ] || ok=0
    [ "${values[14]}" = "$expected_remaining" ] || ok=0
    [ "$status" -eq 0 ] || ok=0

    if [ "$ok" -eq 1 ]; then
        status="ok"
    else
        if [ "${#missing[@]}" -gt 0 ]; then
            printf 'missing_metrics\t%s\t%s\n' "$lane" "${missing[*]}" >&2
        fi
        status="fail:$status"
    fi

    printf '%s\t%s\t%s\t%s\t%s\t%s' \
        "$lane" "$engine" "$backend" "$source" "$FACT_COUNT" "$MATCH_ROUNDS"
    for value in "${values[@]}"; do
        printf '\t%s' "$value"
    done
    printf '\t%s\t%s\t%s\n' "$wall" "$rss" "$status"
}

run_lane() {
    local lane=$1
    local program="$tmp_dir/$lane.metta"
    local log="$tmp_dir/$lane.log"
    local status=0

    case "$lane" in
        cetta-mork)
            render_cetta_case "$lane" "$program" mork
            run_with_time "$lane" "$log" "$CETTA_BIN" --quiet --profile he-extended --lang he "$program" || status=$?
            print_result "$lane" "CeTTa" "explicit MorkSpace" "mork:* direct FFI" "$status" "$log"
            ;;
        cetta-mork-batch)
            render_cetta_case "$lane" "$program" mork-batch
            run_with_time "$lane" "$log" "$CETTA_BIN" --quiet --profile he-extended --lang he "$program" || status=$?
            print_result "$lane" "CeTTa" "explicit MorkSpace" "mork:add-atoms batch FFI" "$status" "$log"
            ;;
        cetta-pathmap)
            render_cetta_case "$lane" "$program" pathmap
            run_with_time "$lane" "$log" "$CETTA_BIN" --quiet --profile he-extended --lang he "$program" || status=$?
            print_result "$lane" "CeTTa" "PathMap ordinary space" "add-atom/match; bridge-backed backend" "$status" "$log"
            ;;
        cetta-native-to-mork)
            render_cetta_space_to_mork_case "$lane" "$program" native
            run_with_time "$lane" "$log" "$CETTA_BIN" --quiet --profile he-extended --lang he "$program" || status=$?
            print_result "$lane" "CeTTa" "native space -> explicit MorkSpace" "native add + get-atoms transfer" "$status" "$log"
            ;;
        cetta-pathmap-to-mork)
            render_cetta_space_to_mork_case "$lane" "$program" pathmap
            run_with_time "$lane" "$log" "$CETTA_BIN" --quiet --profile he-extended --lang he "$program" || status=$?
            print_result "$lane" "CeTTa" "PathMap space -> explicit MorkSpace" "pathmap add + bridge-row transfer" "$status" "$log"
            ;;
        cetta-native)
            render_cetta_case "$lane" "$program" native
            run_with_time "$lane" "$log" "$CETTA_BIN" --quiet --profile he-extended --lang he "$program" || status=$?
            print_result "$lane" "CeTTa" "native ordinary space" "add-atom/match baseline" "$status" "$log"
            ;;
        petta-mork)
            [ -x "$PETTA_DIR/run.sh" ] || die "Missing PeTTa run.sh in $PETTA_DIR"
            render_petta_case "$program" "&mork" 1
            run_with_time "$lane" "$log" "$PETTA_DIR/run.sh" "$program" --silent || status=$?
            print_result "$lane" "PeTTa" "MORK FFI" "recursive &mork" "$status" "$log"
            ;;
        petta-pure)
            [ -x "$PETTA_DIR/run.sh" ] || die "Missing PeTTa run.sh in $PETTA_DIR"
            render_petta_case "$program" "&petta" 0
            run_with_time "$lane" "$log" "$PETTA_DIR/run.sh" "$program" --silent || status=$?
            print_result "$lane" "PeTTa" "pure-space" "ordinary PeTTa space" "$status" "$log"
            ;;
        petta-he-mork)
            [ -x "$PETTA_HE_DIR/run.sh" ] || die "Missing PeTTa --he run.sh in $PETTA_HE_DIR"
            [ -x "$TRANSLATOR" ] || die "Missing translator $TRANSLATOR"
            render_petta_case "$program" "&mork" 1
            "$TRANSLATOR" petta2he "$program" "$tmp_dir/$lane.he.metta" >/dev/null
            run_with_time "$lane" "$log" "$PETTA_HE_DIR/run.sh" --he "$tmp_dir/$lane.he.metta" --silent || status=$?
            print_result "$lane" "PeTTa --he" "MORK FFI" "translated recursive PeTTa &mork" "$status" "$log"
            ;;
        *)
            die "Unknown lane: $lane"
            ;;
    esac
}

printf 'lane\tengine\tbackend\tsource\tfact_count\tmatch_rounds\tinsert_rows\tinsert_seconds\tsource_insert_seconds\tbridge_load_seconds\texact_hit_rows\texact_hit_seconds\texact_miss_rows\texact_miss_seconds\tfull_scan_total_rows\tfull_scan_seconds\tremove_rows\tremove_seconds\tpost_remove_exact_rows\tpost_remove_exact_seconds\tpost_remove_count_rows\tpost_remove_count_seconds\ttotal_seconds\twall_seconds\trss_kb\tstatus\n'
for lane in "${LANES[@]}"; do
    run_lane "$lane"
done
