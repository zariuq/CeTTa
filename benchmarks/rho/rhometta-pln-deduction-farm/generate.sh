#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: $0 <size> <out-dir>   (env: CHAIN_LEN=64 THREADS=1)" >&2
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

chain_len="${CHAIN_LEN:-64}"
threads="${THREADS:-1}"

mkdir -p "$out_dir"
name="deduction_farm_${n}_l${chain_len}_t${threads}"
metta="$out_dir/$name.metta"

{
    printf '!(import! &self rhometta)\n'
    printf '!(import! &self lib_pln)\n'
    for i in $(seq 0 $((n - 1))); do
        for k in $(seq 1 "$chain_len"); do
            if [ $(((i + k) % 2)) -eq 0 ]; then
                printf '(link-tv %s %s (stv 0.8 0.9))\n' "$i" "$k"
            else
                printf '(link-tv %s %s (stv 0.7 0.9))\n' "$i" "$k"
            fi
        done
    done
    printf '(= (chan $n)\n'
    printf '   (if (== $n 0)\n'
    printf '       (rho:quote rho:nil)\n'
    printf '       (rho:quote (rho:send (chan (- $n 1)) rho:nil))))\n'
    printf '(= (link-tv-of $i $k) (match &self (link-tv $i $k $t) $t))\n'
    printf '(= (deduce-chain $i $k $acc)\n'
    printf '   (if (== $k 0)\n'
    printf '       (conclusion (job $i) $acc)\n'
    printf '       (deduce-chain $i (- $k 1)\n'
    printf '         (Truth_Deduction (stv 0.5 0.9) (stv 0.5 0.9) (stv 0.5 0.9)\n'
    printf '                          $acc (link-tv-of $i $k)))))\n'
    printf '(= (deduce-job $i $len) (deduce-chain $i (- $len 1) (link-tv-of $i $len)))\n'
    printf '(= (cell $i $len)\n'
    printf '   (rho:par\n'
    printf '     (rho:send (chan $i) (rhometta:eval (deduce-job $i $len)))\n'
    printf '     (rho:recv (chan $i) $y (rho:drop $y))))\n'
    printf '!(pragma! num-threads %s)\n' "$threads"
    printf '!(collapse (rhometta:run (rho:par'
    for i in $(seq 0 $((n - 1))); do
        printf ' (cell %s %s)' "$i" "$chain_len"
    done
    printf ')))\n'
} > "$metta"

printf '%s\t%s\n' "$name" "$metta"
