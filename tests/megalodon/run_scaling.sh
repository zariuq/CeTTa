#!/usr/bin/env bash
# The native kernel's scaling check: four workloads (scaling_workloads.metta:
# many independent declarations, a growing notation table, one lemma cited
# many times, and a rewrite that passes a term nearly equal to its own side),
# each checked by mg-check at sizes 0, N and 2N.  The counted work is exact:
# the kernel's work units, the reader's relation calls (mg-check is built
# with GN_COUNT) and the conversions the tactic engine was asked for.  Above
# the cost of the empty document, doubling the size may at most double the
# work: the workloads are linear by construction, and the limit 2.2 leaves
# room for tables that grow by doubling.
#
# The nearly-equal workload is measured at depths D and 2D, D = 10 by
# default: an engine that repeats a refuted conversion at every level of
# nesting needs about a thousand times the conversions at twice the depth,
# and at these depths it still finishes and is reported.  That engine is at
# hand: mg-check --reference-conversion runs the conversion as Megalodon
# does.  On this workload it must break the law, or the workload no longer
# tells the two apart; and the conversions mg-check reports theorem by
# theorem must add up to the document's.
set -euo pipefail
tool=${1:?usage: run_scaling.sh MG_CHECK CETTA}
cetta=${2:?usage: run_scaling.sh MG_CHECK CETTA}
tool=$(cd "$(dirname "$tool")" && pwd)/$(basename "$tool")
cetta=$(cd "$(dirname "$cetta")" && pwd)/$(basename "$cetta")
dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
n=${MG_SCALING_N:-200}
depth=${MG_SCALING_DEPTH:-10}
limit=2.2
work=$(mktemp -d "${TMPDIR:-/tmp}/mg-scaling.XXXXXX")
cleanup() {
    rm -f -- "$work"/*.mg
    rmdir -- "$work"
}
trap cleanup EXIT

# Kernel work units, relation calls and conversions of one document, or
# "fail fail fail".
counts() {
    local out u r c
    out=$("$tool" --verbose "$1" 2>&1) || { echo "fail fail fail"; return; }
    u=$(sed -n 's/.*, \([0-9]*\) work units).*/\1/p' <<<"$out" | awk '{s += $1} END {print s + 0}')
    r=$(sed -n 's/^reading: \([0-9]*\) relation calls$/\1/p' <<<"$out")
    c=$(sed -n 's/^elaboration: \([0-9]*\) conversions$/\1/p' <<<"$out")
    echo "$u ${r:-none} ${c:-none}"
}
# Growth above the empty document when the size doubles; no growth is 0.
ratio() {
    awk -v a="$1" -v b="$2" -v z="$3" 'BEGIN {
        if (a - z <= 0) print "0.00"; else if (b - z <= 0) print "inf"; else printf "%.2f", (a - z) / (b - z)}'
}
above() { awk -v r="$1" -v l="$limit" 'BEGIN {exit !(r == "inf" || r + 0 > l + 0)}'; }

fail=0
for kind in decls notation shared nearly; do
    m=$n
    [[ $kind == nearly ]] && m=$depth
    sizes=(0 "$m" $((2 * m)))
    u=(); q=(); v=()
    for s in "${sizes[@]}"; do
        (cd "$work" && "$cetta" --lang petta "$dir/scaling_workloads.metta" "$kind" "$s" "$kind-$s.mg" > /dev/null)
        read -r a b c <<<"$(counts "$work/$kind-$s.mg")"
        u+=("$a"); q+=("$b"); v+=("$c")
    done
    if [[ " ${u[*]} " == *" fail "* ]]; then
        echo "FAIL scaling $kind: a workload is not verified"; fail=1; continue
    fi
    if [[ " ${q[*]} " == *" none "* ]]; then
        echo "FAIL scaling $kind: mg-check does not count relation calls (build it with GN_COUNT)"; fail=1; continue
    fi
    if [[ " ${v[*]} " == *" none "* ]]; then
        echo "FAIL scaling $kind: mg-check does not count conversions"; fail=1; continue
    fi
    ru=$(ratio "${u[2]}" "${u[1]}" "${u[0]}")
    rq=$(ratio "${q[2]}" "${q[1]}" "${q[0]}")
    rv=$(ratio "${v[2]}" "${v[1]}" "${v[0]}")
    if above "$ru" || above "$rq" || above "$rv"; then
        echo "FAIL scaling $kind (N = $m): kernel work $ru, reader calls $rq, conversions $rv when the size doubles (limit $limit)"; fail=1
    else
        echo "ok   scaling $kind (N = $m): kernel work $ru, reader calls $rq, conversions $rv when the size doubles"
    fi
    if [[ $kind == nearly ]]; then
        conversions() { sed -n 's/^elaboration: \([0-9]*\) conversions$/\1/p'; }
        r1=$("$tool" --verbose --reference-conversion "$work/$kind-$m.mg" 2>&1 | conversions)
        r2=$("$tool" --verbose --reference-conversion "$work/$kind-$((2 * m)).mg" 2>&1 | conversions)
        rr=$(ratio "${r2:-0}" "${r1:-0}" 0)
        if above "$rr"; then
            echo "ok   scaling $kind (N = $m): Megalodon's own conversion, conversions $rr when the size doubles (above the limit, as it must be)"
        else
            echo "FAIL scaling $kind (N = $m): Megalodon's own conversion stays within the limit ($rr): the workload tells nothing"; fail=1
        fi
        each=$("$tool" --verbose "$work/$kind-$((2 * m)).mg" 2>&1 |
               sed -n 's/^theorem .*, \([0-9]*\) conversions, .*/\1/p' | awk '{s += $1} END {print s + 0}')
        if [[ "$each" == "${v[2]}" ]]; then
            echo "ok   scaling $kind (N = $((2 * m))): the theorems' conversions add up to the document's ($each)"
        else
            echo "FAIL scaling $kind (N = $((2 * m))): the theorems' conversions add up to $each, the document's are ${v[2]}"; fail=1
        fi
    fi
done
exit $fail
