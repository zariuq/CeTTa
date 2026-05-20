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

mkdir -p "$out_dir"
name="demand_index_$n"
metta="$out_dir/$name.metta"
rho="$out_dir/$name.rho"

{
    printf '!(import! &self rho)\n\n'
    printf '(= (watch-proc (watch $topic $sink))\n'
    printf '   (rho.recv $topic $msg (rho.send $sink (rho.drop $msg))))\n\n'
    printf '(= (fact-proc (fact $topic $payload))\n'
    printf '   (rho.send $topic (rho.send $payload rho.nil)))\n\n'
    printf '(= (build-watches $watches)\n'
    printf '   (foldl-atom $watches rho.nil $acc $watch\n'
    printf '     (let $head (eval (watch-proc $watch))\n'
    printf '       (rho.par $acc $head))))\n\n'
    printf '(= (build-facts $facts)\n'
    printf '   (foldl-atom $facts rho.nil $acc $fact\n'
    printf '     (let $head (eval (fact-proc $fact))\n'
    printf '       (rho.par $acc $head))))\n\n'
    printf '(= (demand-program $watches $facts)\n'
    printf '   (let $watch-procs (eval (build-watches $watches))\n'
    printf '     (let $fact-procs (eval (build-facts $facts))\n'
    printf '       (rho.par $watch-procs $fact-procs))))\n\n'
    printf '!(let $program\n'
    printf '  (eval (demand-program\n'
    printf '    ('
    for i in $(seq 0 $((n - 1))); do
        topic=$((i % 4))
        if [ "$i" -gt 0 ]; then
            printf '\n     '
        fi
        printf '(watch $topic%s $sink%s)' "$topic" "$i"
    done
    printf ')\n'
    printf '    ('
    for i in $(seq 0 $((n - 1))); do
        topic=$((i % 4))
        if [ "$i" -gt 0 ]; then
            printf '\n     '
        fi
        printf '(fact $topic%s $payload%s)' "$topic" "$i"
    done
    printf ')))\n'
    printf '  (rho.step $program))\n'
} > "$metta"

{
    for i in $(seq 0 $((n - 1))); do
        topic=$((i % 4))
        if [ "$i" -gt 0 ]; then
            printf ' |\n'
        fi
        printf 'for (x%s <- @"topic%s") { @"sink%s"!(*x%s) }' "$i" "$topic" "$i" "$i"
    done
    for i in $(seq 0 $((n - 1))); do
        topic=$((i % 4))
        printf ' |\n@"topic%s"!(@"payload%s"!(Nil))' "$topic" "$i"
    done
    printf '\n'
} > "$rho"

printf '%s\t%s\t%s\n' "$name" "$metta" "$rho"
