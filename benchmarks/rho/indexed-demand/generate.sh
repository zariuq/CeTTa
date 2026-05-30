#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: $0 <size> <out-dir>" >&2
}

if [ "$#" -ne 2 ]; then
    usage
    exit 2
fi

n="$1"
out_dir="$2"
case "$n" in
    ''|*[!0-9]*)
        usage
        exit 2
        ;;
esac

target="${TARGET_TOPIC:-0}"
case "$target" in
    0|1|2|3) ;;
    *)
        echo "error: TARGET_TOPIC must be 0, 1, 2, or 3" >&2
        exit 2
        ;;
esac

mkdir -p "$out_dir"
name="indexed_demand_${n}_topic${target}"
metta="$out_dir/$name.metta"
mrho="$out_dir/$name.mrho"
rho="$out_dir/$name.rho"

{
    printf '!(import! &self rho)\n\n'
    printf '(= (watch-proc $target (watch $topic-tag $topic-name $sink))\n'
    printf '   (if-equal $topic-tag $target\n'
    printf '     (rho.recv $topic-name $msg (rho.send $sink (rho.drop $msg)))\n'
    printf '     rho.nil))\n\n'
    printf '(= (fact-proc $target (fact $topic-tag $topic-name $payload))\n'
    printf '   (if-equal $topic-tag $target\n'
    printf '     (rho.send $topic-name (rho.send $payload rho.nil))\n'
    printf '     rho.nil))\n\n'
    printf '(= (build-watches $target $watches)\n'
    printf '   (foldl-atom $watches rho.nil $acc $watch\n'
    printf '     (let $head (eval (watch-proc $target $watch))\n'
    printf '       (rho.par $acc $head))))\n\n'
    printf '(= (build-facts $target $facts)\n'
    printf '   (foldl-atom $facts rho.nil $acc $fact\n'
    printf '     (let $head (eval (fact-proc $target $fact))\n'
    printf '       (rho.par $acc $head))))\n\n'
    printf '(= (indexed-demand-program $target $watches $facts)\n'
    printf '   (let $watch-procs (eval (build-watches $target $watches))\n'
    printf '     (let $fact-procs (eval (build-facts $target $facts))\n'
    printf '       (rho.par $watch-procs $fact-procs))))\n\n'
    printf '!(let $program\n'
    printf '  (eval (indexed-demand-program topic%s\n' "$target"
    printf '    ('
    for i in $(seq 0 $((n - 1))); do
        topic=$((i % 4))
        if [ "$i" -gt 0 ]; then
            printf '\n     '
        fi
        printf '(watch topic%s $topic%s $sink%s)' "$topic" "$topic" "$i"
    done
    printf ')\n'
    printf '    ('
    for i in $(seq 0 $((n - 1))); do
        topic=$((i % 4))
        if [ "$i" -gt 0 ]; then
            printf '\n     '
        fi
        printf '(fact topic%s $topic%s $payload%s)' "$topic" "$topic" "$i"
    done
    printf ')))\n'
    printf '  $program)\n'
} > "$metta"

{
    printf '(rho:par\n'
    for i in $(seq 0 $((n - 1))); do
        topic=$((i % 4))
        if [ "$topic" -ne "$target" ]; then
            continue
        fi
        printf '  (rho:recv $topic%s $msg (rho:send $sink%s (rho:drop $msg)))\n' \
            "$target" "$i"
    done
    for i in $(seq 0 $((n - 1))); do
        topic=$((i % 4))
        if [ "$topic" -ne "$target" ]; then
            continue
        fi
        printf '  (rho:send $topic%s (rho:send $payload%s rho:nil))\n' \
            "$target" "$i"
    done
    printf ')\n'
} > "$mrho"

first=1
{
    for i in $(seq 0 $((n - 1))); do
        topic=$((i % 4))
        if [ "$topic" -ne "$target" ]; then
            continue
        fi
        if [ "$first" -eq 0 ]; then
            printf ' |\n'
        fi
        first=0
        printf 'for (x%s <- @"topic%s") { @"sink%s"!(*x%s) }' \
            "$i" "$target" "$i" "$i"
    done
    for i in $(seq 0 $((n - 1))); do
        topic=$((i % 4))
        if [ "$topic" -ne "$target" ]; then
            continue
        fi
        if [ "$first" -eq 0 ]; then
            printf ' |\n'
        fi
        first=0
        printf '@"topic%s"!(@"payload%s"!(Nil))' "$target" "$i"
    done
    printf '\n'
} > "$rho"

printf '%s\t%s\t%s\t%s\n' "$name" "$metta" "$mrho" "$rho"
