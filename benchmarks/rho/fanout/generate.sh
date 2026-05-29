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
name="fanout_$n"
mrho="$out_dir/$name.mrho"
metta="$out_dir/$name.metta"
rho="$out_dir/$name.rho"

{
    printf '(rho:par\n'
    printf '  (rho:send $bcast rho:nil)\n'
    printf '  (rho:recv $bcast $msg\n'
    printf '    (rho:par\n'
    for i in $(seq 0 $((n - 1))); do
        printf '      (rho:send $c%s (rho:drop $msg))\n' "$i"
    done
    printf '    ))\n'
    for i in $(seq 0 $((n - 1))); do
        printf '  (rho:recv $c%s $sink rho:nil)\n' "$i"
    done
    printf ')\n'
} > "$mrho"

{
    printf '!(import! &self rho)\n\n'
    printf '(= (fanout-source $bcast $payload)\n'
    printf '   (rho.send $bcast $payload))\n\n'
    printf '(= (fanout-sink $chan)\n'
    printf '   (rho.recv $chan $sink rho.nil))\n\n'
    printf '(= (fanout-send $chan $msg)\n'
    printf '   (rho.send $chan (rho.drop $msg)))\n\n'
    printf '!(rho.step\n'
    printf '  (rho.par\n'
    printf '    (fanout-source $bcast rho.nil)\n'
    printf '    (rho.recv $bcast $msg\n'
    printf '      (rho.par\n'
    for i in $(seq 0 $((n - 1))); do
        printf '        (fanout-send $c%s $msg)\n' "$i"
    done
    printf '      ))\n'
    for i in $(seq 0 $((n - 1))); do
        printf '    (fanout-sink $c%s)\n' "$i"
    done
    printf '  ))\n'
} > "$metta"

{
    printf '@"bcast"!(Nil) |\n'
    printf 'for (x <- @"bcast") {\n'
    for i in $(seq 0 $((n - 1))); do
        if [ "$i" -gt 0 ]; then
            printf ' |\n'
        fi
        printf '  @"c%s"!(*x)' "$i"
    done
    printf '\n}'
    for i in $(seq 0 $((n - 1))); do
        printf ' |\nfor (_ <- @"c%s") { Nil }' "$i"
    done
    printf '\n'
} > "$rho"

printf '%s\t%s\t%s\t%s\n' "$name" "$mrho" "$metta" "$rho"
