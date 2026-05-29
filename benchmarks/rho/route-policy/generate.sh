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

policy="${ROUTE_POLICY:-trusted}"
case "$policy" in
    trusted|backup) ;;
    *)
        echo "error: ROUTE_POLICY must be trusted or backup" >&2
        exit 2
        ;;
esac

mkdir -p "$out_dir"
name="route_policy_${n}_${policy}"
metta="$out_dir/$name.metta"
rho="$out_dir/$name.rho"

edge_tag() {
    local i="$1"
    if [ $((i % 3)) -eq 2 ]; then
        printf 'backup'
    else
        printf 'trusted'
    fi
}

{
    printf '!(import! &self rho)\n\n'
    printf '(= (edge-forwarder-if $required (edge $src $dst $tag))\n'
    printf '   (if-equal $tag $required\n'
    printf '     (rho.recv $src $msg (rho.send $dst (rho.drop $msg)))\n'
    printf '     rho.nil))\n\n'
    printf '(= (edge-signal-if $required (edge $src $dst $tag) $payload)\n'
    printf '   (if-equal $tag $required\n'
    printf '     (rho.send $src (rho.send $payload rho.nil))\n'
    printf '     rho.nil))\n\n'
    printf '(= (build-forwarders $required $edges)\n'
    printf '   (foldl-atom $edges rho.nil $acc $edge\n'
    printf '     (let $head (eval (edge-forwarder-if $required $edge))\n'
    printf '       (rho.par $acc $head))))\n\n'
    printf '(= (build-signals $required $edges $payload)\n'
    printf '   (foldl-atom $edges rho.nil $acc $edge\n'
    printf '     (let $head (eval (edge-signal-if $required $edge $payload))\n'
    printf '       (rho.par $acc $head))))\n\n'
    printf '(= (route-policy-program $required $edges $payload)\n'
    printf '   (let $forwarders (eval (build-forwarders $required $edges))\n'
    printf '     (let $signals (eval (build-signals $required $edges $payload))\n'
    printf '       (rho.par $forwarders $signals))))\n\n'
    printf '!(let $program\n'
    printf '  (eval (route-policy-program %s\n' "$policy"
    printf '    ('
    for i in $(seq 0 $((n - 1))); do
        next=$((i + 1))
        tag="$(edge_tag "$i")"
        if [ "$i" -gt 0 ]; then
            printf '\n     '
        fi
        printf '(edge $c%s $c%s %s)' "$i" "$next" "$tag"
    done
    printf ')\n'
    printf '    $payload))\n'
    printf '  (rho.step $program))\n'
} > "$metta"

first=1
{
    for i in $(seq 0 $((n - 1))); do
        tag="$(edge_tag "$i")"
        if [ "$tag" != "$policy" ]; then
            continue
        fi
        next=$((i + 1))
        if [ "$first" -eq 0 ]; then
            printf ' |\n'
        fi
        first=0
        printf 'for (x%s <- @"c%s") { @"c%s"!(*x%s) }' "$i" "$i" "$next" "$i"
    done
    for i in $(seq 0 $((n - 1))); do
        tag="$(edge_tag "$i")"
        if [ "$tag" != "$policy" ]; then
            continue
        fi
        if [ "$first" -eq 0 ]; then
            printf ' |\n'
        fi
        first=0
        printf '@"c%s"!(@"payload"!(Nil))' "$i"
    done
    printf '\n'
} > "$rho"

printf '%s\t%s\t%s\n' "$name" "$metta" "$rho"
