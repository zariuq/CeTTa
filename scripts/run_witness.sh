#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage:
  scripts/run_witness.sh <name>
  scripts/run_witness.sh --list
  scripts/run_witness.sh --show-baselines

Runs a named witness from perf/witness_catalog.tsv with the recorded timeout and
memory budget, then prints a stable machine-readable summary plus a TSV record
line that can be copied into perf/baseline_records.tsv when desired.
EOF
}

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
catalog="$repo_root/perf/witness_catalog.tsv"
baselines="$repo_root/perf/baseline_records.tsv"

if [[ "${1:-}" == "--list" ]]; then
    cat "$catalog"
    exit 0
fi

if [[ "${1:-}" == "--show-baselines" ]]; then
    cat "$baselines"
    exit 0
fi

if [[ $# -ne 1 ]]; then
    usage >&2
    exit 2
fi

name="$1"
row="$(awk -F '\t' -v key="$name" 'NR > 1 && $1 == key { print; exit }' "$catalog")"
if [[ -z "$row" ]]; then
    echo "unknown witness: $name" >&2
    exit 2
fi

IFS=$'\t' read -r witness_name category build_hint timeout_s mem_kib command notes <<< "$row"

witness_bin="./cetta"
witness_build_config="./runtime/bootstrap/build_config.core.h"

ensure_build_from_hint() {
    local hint="$1"
    local build_mode=""
    local enable_runtime_stats=0
    local runtime_target=""

    case "$hint" in
        core|python|mork|main|pathmap|full)
            build_mode="$hint"
            ;;
        profile-core|profile-python|profile-mork|profile-main|profile-pathmap|profile-full)
            build_mode="${hint#profile-}"
            enable_runtime_stats=1
            ;;
        *)
            echo "unsupported build_hint: $hint" >&2
            exit 2
            ;;
    esac

    if [[ "$enable_runtime_stats" -eq 1 ]]; then
        runtime_target="runtime/cetta-${build_mode}-runtime-stats"
        (cd "$repo_root" && ulimit -v "$mem_kib" && \
            make -j1 BUILD="$build_mode" ENABLE_RUNTIME_STATS=1 "$runtime_target" >/dev/null)
        witness_bin="./$runtime_target"
        witness_build_config="./runtime/bootstrap/build_config.${build_mode}.runtime-stats.h"
    else
        (cd "$repo_root" && ulimit -v "$mem_kib" && \
            make -j1 BUILD="$build_mode" ENABLE_RUNTIME_STATS=0 cetta >/dev/null)
        witness_bin="./cetta"
        witness_build_config="./runtime/bootstrap/build_config.${build_mode}.h"
    fi
}

ensure_build_from_hint "$build_hint"
run_command="$command"
if [[ "$witness_bin" != "./cetta" ]]; then
    run_command="${run_command//.\/cetta/$witness_bin}"
fi
export CETTA_BIN="$repo_root/${witness_bin#./}"
export CETTA_BUILD_CONFIG="$repo_root/${witness_build_config#./}"

if git -C "$repo_root" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    commit="$(git -C "$repo_root" rev-parse --short HEAD)"
    if git -C "$repo_root" diff --quiet --ignore-submodules HEAD -- &&
       git -C "$repo_root" diff --quiet --ignore-submodules --cached HEAD --; then
        tree_state="clean"
    else
        tree_state="dirty"
    fi
else
    commit="unknown"
    tree_state="nogit"
fi

logfile="$repo_root/.witness_${witness_name}_$$.log"
trap 'rm -f "$logfile"' EXIT

set +e
(
    cd "$repo_root"
    ulimit -v "$mem_kib"
    /usr/bin/time -f 'ELAPSED=%E\nRSS_KB=%M\nEXIT=%x' \
        timeout "$timeout_s" bash -lc "$run_command"
) >"$logfile" 2>&1
shell_status=$?
set -e

elapsed="$(awk -F= '/^ELAPSED=/{print $2}' "$logfile" | tail -1)"
rss_kib="$(awk -F= '/^RSS_KB=/{print $2}' "$logfile" | tail -1)"
inner_exit="$(awk -F= '/^EXIT=/{print $2}' "$logfile" | tail -1)"

if [[ -z "$inner_exit" ]]; then
    inner_exit="$shell_status"
fi

status="fail"
if [[ "$shell_status" -eq 0 && "$inner_exit" == "0" ]]; then
    status="pass"
elif [[ "$shell_status" -eq 124 || "$inner_exit" == "124" ]]; then
    status="timeout"
fi

last_payload="$(awk '
    /^[A-Z_]+=/{next}
    { line = $0 }
    END { print line }
' "$logfile")"
last_payload="${last_payload//$'\t'/ }"

first_surface_error="$(awk '
    /^[A-Z_]+=/{next}
    index($0, "(Error ") > 0 { print; exit }
' "$logfile")"
first_surface_error="${first_surface_error//$'\t'/ }"

status_reason="exit-status"
if [[ "$status" == "timeout" ]]; then
    status_reason="timeout"
elif [[ "$status" == "pass" && -n "$first_surface_error" ]]; then
    status="fail"
    status_reason="surface-error-payload"
fi

printf 'NAME=%s\n' "$witness_name"
printf 'CATEGORY=%s\n' "$category"
printf 'BUILD_HINT=%s\n' "$build_hint"
printf 'COMMIT=%s\n' "$commit"
printf 'TREE_STATE=%s\n' "$tree_state"
printf 'STATUS=%s\n' "$status"
printf 'STATUS_REASON=%s\n' "$status_reason"
printf 'TIMEOUT_S=%s\n' "$timeout_s"
printf 'MEM_KIB=%s\n' "$mem_kib"
printf 'ELAPSED=%s\n' "${elapsed:-unknown}"
printf 'RSS_KB=%s\n' "${rss_kib:-unknown}"
printf 'COMMAND=%s\n' "$run_command"
printf 'NOTES=%s\n' "$notes"
printf 'LAST_PAYLOAD=%s\n' "${last_payload:-}"
printf 'FIRST_SURFACE_ERROR=%s\n' "${first_surface_error:-}"
printf 'TSV_RECORD=%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$witness_name" \
    "$commit" \
    "$tree_state" \
    "$status" \
    "${elapsed:-unknown}" \
    "${rss_kib:-unknown}" \
    "$build_hint" \
    "$timeout_s" \
    "$mem_kib" \
    "${last_payload:-}"

if [[ "$shell_status" -ne 0 && "$shell_status" -ne 124 ]]; then
    cat "$logfile" >&2
fi

exit 0
