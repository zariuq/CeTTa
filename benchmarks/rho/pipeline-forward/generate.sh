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
name="pipeline_forward_$n"
mrho="$out_dir/$name.mrho"
metta="$out_dir/$name.metta"
rho="$out_dir/$name.rho"

{
    printf '(rho:par\n'
    printf '  (rho:send $c0 rho:nil)\n'
    for i in $(seq 0 $((n - 1))); do
        next=$((i + 1))
        printf '  (rho:recv $c%s $msg (rho:send $c%s (rho:drop $msg)))\n' "$i" "$next"
    done
    printf '  (rho:recv $c%s $done rho:nil)\n' "$n"
    printf ')\n'
} > "$mrho"

{
    printf '!(import! &self rho)\n\n'
    printf '(= (pipeline-source $chan)\n'
    printf '   (rho.send $chan rho.nil))\n\n'
    printf '(= (pipeline-sink $chan)\n'
    printf '   (rho.recv $chan $done rho.nil))\n\n'
    printf '(= (fwd $src $dst)\n'
    printf '   (rho.recv $src $msg (rho.send $dst (rho.drop $msg))))\n\n'
    printf '!(rho.step\n'
    printf '  (rho.par\n'
    printf '    (pipeline-source $c0)\n'
    for i in $(seq 0 $((n - 1))); do
        next=$((i + 1))
        printf '    (fwd $c%s $c%s)\n' "$i" "$next"
    done
    printf '    (pipeline-sink $c%s)\n' "$n"
    printf '  ))\n'
} > "$metta"

{
    printf '@"c0"!(Nil)'
    for i in $(seq 0 $((n - 1))); do
        next=$((i + 1))
        printf ' |\nfor (x%s <- @"c%s") { @"c%s"!(*x%s) }' "$i" "$i" "$next" "$i"
    done
    printf ' |\nfor (_ <- @"c%s") { Nil }\n' "$n"
} > "$rho"

printf '%s\t%s\t%s\t%s\n' "$name" "$mrho" "$metta" "$rho"
