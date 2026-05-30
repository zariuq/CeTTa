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
name="route_synthesis_$n"
metta="$out_dir/$name.metta"
mrho="$out_dir/$name.mrho"
rho="$out_dir/$name.rho"

{
    printf '!(import! &self rho)\n\n'
    printf '(= (edge-forwarder (edge $src $dst))\n'
    printf '   (rho.recv $src $msg (rho.send $dst (rho.drop $msg))))\n\n'
    printf '(= (edge-signal (edge $src $dst) $payload)\n'
    printf '   (rho.send $src (rho.send $payload rho.nil)))\n\n'
    printf '(= (build-forwarders $edges)\n'
    printf '   (foldl-atom $edges rho.nil $acc $edge\n'
    printf '     (let $head (eval (edge-forwarder $edge))\n'
    printf '       (rho.par $acc $head))))\n\n'
    printf '(= (build-signals $edges $payload)\n'
    printf '   (foldl-atom $edges rho.nil $acc $edge\n'
    printf '     (let $head (eval (edge-signal $edge $payload))\n'
    printf '       (rho.par $acc $head))))\n\n'
    printf '(= (route-program $edges $payload)\n'
    printf '   (let $forwarders (eval (build-forwarders $edges))\n'
    printf '     (let $signals (eval (build-signals $edges $payload))\n'
    printf '       (rho.par $forwarders $signals))))\n\n'
    printf '!(let $program\n'
    printf '  (eval (route-program\n'
    printf '    ('
    for i in $(seq 0 $((n - 1))); do
        next=$((i + 1))
        if [ "$i" -gt 0 ]; then
            printf '\n     '
        fi
        printf '(edge $c%s $c%s)' "$i" "$next"
    done
    printf ')\n'
    printf '    $payload))\n'
    printf '  $program)\n'
} > "$metta"

{
    printf '(rho:par\n'
    for i in $(seq 0 $((n - 1))); do
        next=$((i + 1))
        printf '  (rho:recv $c%s $msg (rho:send $c%s (rho:drop $msg)))\n' "$i" "$next"
    done
    for i in $(seq 0 $((n - 1))); do
        printf '  (rho:send $c%s (rho:send $payload rho:nil))\n' "$i"
    done
    printf ')\n'
} > "$mrho"

{
    for i in $(seq 0 $((n - 1))); do
        if [ "$i" -gt 0 ]; then
            printf ' |\n'
        fi
        next=$((i + 1))
        printf 'for (x%s <- @"c%s") { @"c%s"!(*x%s) }' "$i" "$i" "$next" "$i"
    done
    for i in $(seq 0 $((n - 1))); do
        printf ' |\n@"c%s"!(@"payload"!(Nil))' "$i"
    done
    printf '\n'
} > "$rho"

printf '%s\t%s\t%s\t%s\n' "$name" "$metta" "$mrho" "$rho"
