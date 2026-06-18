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
name="hot_successors_$n"
mrho="$out_dir/$name.mrho"
metta="$out_dir/$name.metta"
rho="$out_dir/$name.rho"

{
    printf '(rho:par\n'
    for i in $(seq 0 $((n - 1))); do
        printf '  (rho:recv $hot $msg (rho:send $out%s (rho:drop $msg)))\n' "$i"
    done
    for i in $(seq 0 $((n - 1))); do
        printf '  (rho:send $hot (rho:send $p%s rho:nil))\n' "$i"
    done
    printf ')\n'
} > "$mrho"

{
    printf '!(import! &self rho)\n\n'
    printf '(= (hot-recv $chan $out)\n'
    printf '   (rho.recv $chan $msg (rho.send $out (rho.drop $msg))))\n\n'
    printf '(= (hot-send $chan $payload)\n'
    printf '   (rho.send $chan (rho.send $payload rho.nil)))\n\n'
    printf '!(let $program\n'
    printf '  (eval\n'
    printf '    (rho.par\n'
    for i in $(seq 0 $((n - 1))); do
        printf '      (hot-recv $hot $out%s)\n' "$i"
    done
    for i in $(seq 0 $((n - 1))); do
        printf '      (hot-send $hot $p%s)\n' "$i"
    done
    printf '    ))\n'
    printf '  $program)\n'
} > "$metta"

{
    for i in $(seq 0 $((n - 1))); do
        if [ "$i" -gt 0 ]; then
            printf ' |\n'
        fi
        printf 'for (x%s <- @"hot") { @"out%s"!(*x%s) }' "$i" "$i" "$i"
    done
    for i in $(seq 0 $((n - 1))); do
        printf ' |\n@"hot"!(@"p%s"!(Nil))' "$i"
    done
    printf '\n'
} > "$rho"

printf '%s\t%s\t%s\t%s\n' "$name" "$mrho" "$metta" "$rho"
