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
name="private_session_$n"
metta="$out_dir/$name.metta"
mrho="$out_dir/$name.mrho"
rho="$out_dir/$name.rho"

{
    printf '!(import! &self rho)\n\n'
    printf '(= (private-session $req $reply $out $payload)\n'
    printf '   (rho.par\n'
    printf '     (rho.recv $req $msg (rho.send $reply (rho.drop $msg)))\n'
    printf '     (rho.send $req (rho.send $payload rho.nil))\n'
    printf '     (rho.recv $reply $msg (rho.send $out (rho.drop $msg)))))\n\n'
    printf '!(let $program\n'
    printf '  (eval\n'
    printf '    (rho.par\n'
    for i in $(seq 0 $((n - 1))); do
        printf '      (private-session $req%s $reply%s $out%s $payload%s)\n' \
            "$i" "$i" "$i" "$i"
    done
    printf '      rho.nil))\n'
    printf '  $program)\n'
} > "$metta"

{
    printf '(rho:par\n'
    for i in $(seq 0 $((n - 1))); do
        printf '  (rho:par\n'
        printf '    (rho:recv $req%s $msg (rho:send $reply%s (rho:drop $msg)))\n' \
            "$i" "$i"
        printf '    (rho:send $req%s (rho:send $payload%s rho:nil))\n' "$i" "$i"
        printf '    (rho:recv $reply%s $msg (rho:send $out%s (rho:drop $msg))))\n' \
            "$i" "$i"
    done
    printf '  rho:nil)\n'
} > "$mrho"

{
    first=1
    for i in $(seq 0 $((n - 1))); do
        if [ "$first" -eq 0 ]; then
            printf ' |\n'
        fi
        first=0
        printf 'for (x%s <- @"req%s") { @"reply%s"!(*x%s) } |\n' "$i" "$i" "$i" "$i"
        printf '@"req%s"!(@"payload%s"!(Nil)) |\n' "$i" "$i"
        printf 'for (y%s <- @"reply%s") { @"out%s"!(*y%s) }' "$i" "$i" "$i" "$i"
    done
    printf '\n'
} > "$rho"

printf '%s\t%s\t%s\t%s\n' "$name" "$metta" "$mrho" "$rho"
