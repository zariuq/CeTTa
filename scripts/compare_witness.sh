#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage:
  scripts/compare_witness.sh <name>

Runs a witness via scripts/run_witness.sh, selects the nearest recorded
ancestor baseline for the current HEAD automatically, and prints a compact
comparison summary.
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

best_line=""
best_distance=""

while IFS=$'\t' read -r row_name repo_label commit status elapsed rss_kib build_hint timeout_s mem_kib note; do
    [[ "$row_name" == "name" ]] && continue
    [[ "$row_name" != "$name" ]] && continue
    if git -C "$repo_root" merge-base --is-ancestor "$commit" HEAD 2>/dev/null; then
        distance="$(git -C "$repo_root" rev-list --count "${commit}..HEAD")"
        if [[ -z "$best_distance" || "$distance" -lt "$best_distance" ]]; then
            best_distance="$distance"
            best_line="$repo_label"$'\t'"$commit"$'\t'"$status"$'\t'"$elapsed"$'\t'"$rss_kib"$'\t'"$note"
        fi
    fi
done < "$baseline_file"

printf '%s\n' "$run_output"

if [[ -z "$best_line" ]]; then
    printf 'COMPARE_BASELINE=none\n'
    exit 0
fi

IFS=$'\t' read -r base_repo base_commit base_status base_elapsed base_rss base_note <<< "$best_line"

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
base_ms="$(elapsed_to_ms "$base_elapsed")"

printf 'COMPARE_BASELINE_REPO=%s\n' "$base_repo"
printf 'COMPARE_BASELINE_COMMIT=%s\n' "$base_commit"
printf 'COMPARE_BASELINE_DISTANCE=%s\n' "$best_distance"
printf 'COMPARE_BASELINE_STATUS=%s\n' "$base_status"
printf 'COMPARE_BASELINE_ELAPSED=%s\n' "$base_elapsed"
printf 'COMPARE_BASELINE_RSS_KB=%s\n' "$base_rss"
printf 'COMPARE_BASELINE_NOTE=%s\n' "$base_note"

if [[ -n "$cur_ms" && -n "$base_ms" && "$base_ms" -gt 0 ]]; then
    ratio="$(awk -v cur="$cur_ms" -v base="$base_ms" 'BEGIN { printf "%.3f", cur / base }')"
    delta_ms=$((cur_ms - base_ms))
    printf 'COMPARE_ELAPSED_RATIO=%s\n' "$ratio"
    printf 'COMPARE_ELAPSED_DELTA_MS=%s\n' "$delta_ms"
fi

if [[ -n "$current_rss" && -n "$base_rss" && "$current_rss" != "unknown" && "$base_rss" != "unknown" ]]; then
    printf 'COMPARE_RSS_DELTA_KB=%s\n' "$((current_rss - base_rss))"
fi

if [[ "$current_status" == "$base_status" ]]; then
    printf 'COMPARE_STATUS_SUMMARY=status-match\n'
else
    printf 'COMPARE_STATUS_SUMMARY=status-diff\n'
fi

printf 'COMPARE_TREE_STATE=%s\n' "$current_tree"
