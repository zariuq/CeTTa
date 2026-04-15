#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage:
  scripts/compare_witness.sh <name>

Runs a witness via scripts/run_witness.sh, compares it against a recorded
solid-anchor baseline when one exists for the current HEAD, and also reports
the nearest contextual checkpoint separately.
EOF
}

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
baseline_file="$repo_root/perf/baseline_records.tsv"

if [[ $# -ne 1 ]]; then
    usage >&2
    exit 2
fi

name="$1"
run_output="$("$repo_root/scripts/run_witness.sh" "$name")"

current_commit="$(printf '%s\n' "$run_output" | awk -F= '/^COMMIT=/{print $2; exit}')"
current_status="$(printf '%s\n' "$run_output" | awk -F= '/^STATUS=/{print $2; exit}')"
current_elapsed="$(printf '%s\n' "$run_output" | awk -F= '/^ELAPSED=/{print $2; exit}')"
current_rss="$(printf '%s\n' "$run_output" | awk -F= '/^RSS_KB=/{print $2; exit}')"
current_tree="$(printf '%s\n' "$run_output" | awk -F= '/^TREE_STATE=/{print $2; exit}')"

elapsed_to_ms() {
    local t="$1"
    if [[ "$t" == "unknown" || -z "$t" ]]; then
        echo ""
        return 0
    fi
    awk -v t="$t" '
        BEGIN {
            n = split(t, a, ":");
            if (n == 2) {
                mins = a[1] + 0;
                secs = a[2] + 0;
                printf "%.0f\n", (mins * 60 + secs) * 1000;
            } else if (n == 3) {
                hrs = a[1] + 0;
                mins = a[2] + 0;
                secs = a[3] + 0;
                printf "%.0f\n", (hrs * 3600 + mins * 60 + secs) * 1000;
            }
        }'
}

cur_ms="$(elapsed_to_ms "$current_elapsed")"

status_rank() {
    case "$1" in
        pass) echo 0 ;;
        timeout) echo 1 ;;
        *) echo 2 ;;
    esac
}

baseline_line=""
baseline_rank=""
baseline_ms=""
baseline_distance=""
context_line=""
context_distance=""

while IFS=$'\t' read -r row_name repo_label commit status elapsed rss_kib build_hint timeout_s mem_kib note role; do
    [[ "$row_name" == "name" ]] && continue
    [[ "$row_name" != "$name" ]] && continue
    if ! git -C "$repo_root" merge-base --is-ancestor "$commit" HEAD 2>/dev/null; then
        continue
    fi
    role="${role:-context}"
    distance="$(git -C "$repo_root" rev-list --count "${commit}..HEAD")"
    if [[ "$role" == "solid_anchor" ]]; then
        candidate_rank="$(status_rank "$status")"
        candidate_ms="$(elapsed_to_ms "$elapsed")"
        if [[ -z "$baseline_line" ]] ||
           [[ "$candidate_rank" -lt "$baseline_rank" ]] ||
           [[ "$candidate_rank" -eq "$baseline_rank" &&
              -n "$candidate_ms" &&
              ( -z "$baseline_ms" || "$candidate_ms" -lt "$baseline_ms" ) ]] ||
           [[ "$candidate_rank" -eq "$baseline_rank" &&
              ( -z "$candidate_ms" || "$candidate_ms" == "$baseline_ms" ) &&
              ( -z "$baseline_distance" || "$distance" -lt "$baseline_distance" ) ]]; then
            baseline_line="$repo_label"$'\t'"$commit"$'\t'"$status"$'\t'"$elapsed"$'\t'"$rss_kib"$'\t'"$note"$'\t'"$role"
            baseline_rank="$candidate_rank"
            baseline_ms="${candidate_ms:-}"
            baseline_distance="$distance"
        fi
    else
        if [[ -z "$context_distance" || "$distance" -lt "$context_distance" ]]; then
            context_distance="$distance"
            context_line="$repo_label"$'\t'"$commit"$'\t'"$status"$'\t'"$elapsed"$'\t'"$rss_kib"$'\t'"$note"$'\t'"$role"
        fi
    fi
done < "$baseline_file"

print_compare_block() {
    local prefix="$1"
    local line="$2"
    local distance="$3"
    local repo commit status elapsed rss note role base_ms ratio delta_ms

    if [[ -z "$line" ]]; then
        printf '%s=none\n' "$prefix"
        return 0
    fi

    IFS=$'\t' read -r repo commit status elapsed rss note role <<< "$line"
    base_ms="$(elapsed_to_ms "$elapsed")"

    printf '%s_REPO=%s\n' "$prefix" "$repo"
    printf '%s_COMMIT=%s\n' "$prefix" "$commit"
    printf '%s_DISTANCE=%s\n' "$prefix" "$distance"
    printf '%s_ROLE=%s\n' "$prefix" "$role"
    printf '%s_STATUS=%s\n' "$prefix" "$status"
    printf '%s_ELAPSED=%s\n' "$prefix" "$elapsed"
    printf '%s_RSS_KB=%s\n' "$prefix" "$rss"
    printf '%s_NOTE=%s\n' "$prefix" "$note"

    if [[ -n "$cur_ms" && -n "$base_ms" && "$base_ms" -gt 0 ]]; then
        ratio="$(awk -v cur="$cur_ms" -v base="$base_ms" 'BEGIN { printf "%.3f", cur / base }')"
        delta_ms=$((cur_ms - base_ms))
        printf '%s_ELAPSED_RATIO=%s\n' "$prefix" "$ratio"
        printf '%s_ELAPSED_DELTA_MS=%s\n' "$prefix" "$delta_ms"
    fi

    if [[ -n "$current_rss" && -n "$rss" && "$current_rss" != "unknown" && "$rss" != "unknown" ]]; then
        printf '%s_RSS_DELTA_KB=%s\n' "$prefix" "$((current_rss - rss))"
    fi

    if [[ "$current_status" == "$status" ]]; then
        printf '%s_STATUS_SUMMARY=status-match\n' "$prefix"
    else
        printf '%s_STATUS_SUMMARY=status-diff\n' "$prefix"
    fi
}

printf '%s\n' "$run_output"
print_compare_block "COMPARE_BASELINE" "$baseline_line" "$baseline_distance"
print_compare_block "COMPARE_CONTEXT" "$context_line" "$context_distance"
printf 'COMPARE_TREE_STATE=%s\n' "$current_tree"
