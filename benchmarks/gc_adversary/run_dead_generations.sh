#!/usr/bin/env sh
# Dead-generations GC adversary.  Punishes any branch store that retains
# dead-sibling terms until episode end.  Reclaiming implementations hold one
# generation; a non-reclaiming continuation pool holds all of them.
#
# usage: benchmarks/gc_adversary/run_dead_generations.sh [/path/to/PeTTa]
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../.." && pwd)
fixture="$here/dead_generations.metta"
petta_root="${1:-$root/../PeTTa}"
limit_kb=${CETTA_GC_ADVERSARY_LIMIT_KB:-}

measure() {
    label=$1; shift
    out=$(/usr/bin/time -v "$@" 2>&1 >/dev/null || true)
    rss=$(printf '%s\n' "$out" | sed -n 's/.*Maximum resident set size (kbytes): //p')
    secs=$(printf '%s\n' "$out" | sed -n 's/.*Elapsed (wall clock) time.*: //p')
    printf '%-28s rss=%8s KB   wall=%s\n' "$label" "${rss:-?}" "${secs:-?}"
}

survive() {
    label=$1; shift
    if ( ulimit -v "$limit_kb"; "$@" >/dev/null 2>&1 ); then
        printf '%-28s COMPLETED under requested virtual-memory limit\n' "$label"
    else
        printf '%-28s DIED under requested virtual-memory limit\n' "$label"
    fi
}

cd "$root"
measure "cetta inline"  env CETTA_SEARCH_CONTROLLER=inline-depth-first ./cetta --lang petta "$fixture"
measure "cetta fifo"    env CETTA_SEARCH_CONTROLLER=fifo ./cetta --lang petta "$fixture"
measure "cetta ratio:8" env CETTA_SEARCH_CONTROLLER=ratio:8 ./cetta --lang petta "$fixture"
if [ -x "$petta_root/run.sh" ]; then
    ( cd "$petta_root" && measure "swi-petta" ./run.sh "$fixture" --silent )
fi
if [ -n "$limit_kb" ]; then
    survive "cetta inline"  env CETTA_SEARCH_CONTROLLER=inline-depth-first ./cetta --lang petta "$fixture"
    survive "cetta fifo"    env CETTA_SEARCH_CONTROLLER=fifo ./cetta --lang petta "$fixture"
    survive "cetta ratio:8" env CETTA_SEARCH_CONTROLLER=ratio:8 ./cetta --lang petta "$fixture"
    if [ -x "$petta_root/run.sh" ]; then
        ( cd "$petta_root" && survive "swi-petta" ./run.sh "$fixture" --silent )
    fi
fi

a=$(env CETTA_SEARCH_CONTROLLER=inline-depth-first ./cetta --lang petta "$fixture" 2>/dev/null | sort | cksum)
b=$(env CETTA_SEARCH_CONTROLLER=fifo ./cetta --lang petta "$fixture" 2>/dev/null | sort | cksum)
c=$(env CETTA_SEARCH_CONTROLLER=ratio:8 ./cetta --lang petta "$fixture" 2>/dev/null | sort | cksum)
if [ "$a" = "$b" ] && [ "$a" = "$c" ]; then
    echo "answer bags agree"
else
    echo "ANSWER BAGS DIFFER"
    exit 1
fi

stats=$(env CETTA_SEARCH_CONTROLLER=fifo CETTA_PETTA_MACHINE_STATS=1 \
    ./cetta --lang petta "$fixture" 2>&1 >/dev/null)
captures=$(printf '%s\n' "$stats" | sed -n \
    's/.*owned_continuation_captures=\([0-9][0-9]*\).*/\1/p')
captured_vectors=$(printf '%s\n' "$stats" | sed -n \
    's/.*owned_continuation_vector_bytes_captured=\([0-9][0-9]*\).*/\1/p')
max_bindings=$(printf '%s\n' "$stats" | sed -n \
    's/.*max_binding_entries=\([0-9][0-9]*\).*/\1/p')
if [ -z "$captures" ] || [ -z "$captured_vectors" ] || \
   [ -z "$max_bindings" ] || [ "$captures" -le 0 ] || \
   [ "$max_bindings" -gt 64 ] || \
   [ "$captured_vectors" -gt "$((captures * 2048))" ]; then
    echo "CONTINUATION SUPPORT DID NOT STAY BOUNDED"
    printf '%s\n' "$stats"
    exit 1
fi
echo "continuation support stays bounded"
