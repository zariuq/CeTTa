#!/usr/bin/env bash
# The native kernel's scaling check: three workloads (scaling_workloads.metta:
# many independent declarations, a growing notation table, one lemma cited
# many times), each checked by mg-check at sizes 0, N and 2N.  The counted
# work is exact: the kernel's work units and the reader's relation calls
# (mg-check is built with GN_COUNT).  Above the cost of the empty document,
# doubling the size may at most double the work: the workloads are linear by
# construction, and the limit 2.2 leaves room for tables that grow by
# doubling.
set -euo pipefail
tool=${1:?usage: run_scaling.sh MG_CHECK CETTA}
cetta=${2:?usage: run_scaling.sh MG_CHECK CETTA}
tool=$(cd "$(dirname "$tool")" && pwd)/$(basename "$tool")
cetta=$(cd "$(dirname "$cetta")" && pwd)/$(basename "$cetta")
dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
n=${MG_SCALING_N:-200}
limit=2.2
work=$(mktemp -d "${TMPDIR:-/tmp}/mg-scaling.XXXXXX")
cleanup() {
    rm -f -- "$work"/*.mg
    rmdir -- "$work"
}
trap cleanup EXIT

# Kernel work units and relation calls of one document, or "fail fail".
counts() {
    local out u r
    out=$("$tool" --verbose "$1" 2>&1) || { echo "fail fail"; return; }
    u=$(sed -n 's/.*, \([0-9]*\) work units).*/\1/p' <<<"$out" | awk '{s += $1} END {print s + 0}')
    r=$(sed -n 's/^reading: \([0-9]*\) relation calls$/\1/p' <<<"$out")
    echo "$u ${r:-none}"
}
ratio() { awk -v a="$1" -v b="$2" -v z="$3" 'BEGIN {if (b - z <= 0) print "inf"; else printf "%.2f", (a - z) / (b - z)}'; }
above() { awk -v r="$1" -v l="$limit" 'BEGIN {exit !(r == "inf" || r + 0 > l + 0)}'; }

fail=0
for kind in decls notation shared; do
    sizes=(0 "$n" $((2 * n)))
    u=(); q=()
    for s in "${sizes[@]}"; do
        (cd "$work" && "$cetta" --lang petta "$dir/scaling_workloads.metta" "$kind" "$s" "$kind-$s.mg" > /dev/null)
        read -r a b <<<"$(counts "$work/$kind-$s.mg")"
        u+=("$a"); q+=("$b")
    done
    if [[ " ${u[*]} " == *" fail "* ]]; then
        echo "FAIL scaling $kind: a workload is not verified"; fail=1; continue
    fi
    if [[ " ${q[*]} " == *" none "* ]]; then
        echo "FAIL scaling $kind: mg-check does not count relation calls (build it with GN_COUNT)"; fail=1; continue
    fi
    ru=$(ratio "${u[2]}" "${u[1]}" "${u[0]}")
    rq=$(ratio "${q[2]}" "${q[1]}" "${q[0]}")
    if above "$ru" || above "$rq"; then
        echo "FAIL scaling $kind (N = $n): kernel work $ru, reader calls $rq when the size doubles (limit $limit)"; fail=1
    else
        echo "ok   scaling $kind (N = $n): kernel work $ru, reader calls $rq when the size doubles"
    fi
done
exit $fail
