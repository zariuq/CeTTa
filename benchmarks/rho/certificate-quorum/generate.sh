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

required="${CERT_STATUS:-valid}"
case "$required" in
    valid|stale) ;;
    *)
        echo "error: CERT_STATUS must be valid or stale" >&2
        exit 2
        ;;
esac

mkdir -p "$out_dir"
name="certificate_quorum_${n}_${required}"
metta="$out_dir/$name.metta"
mrho="$out_dir/$name.mrho"
rho="$out_dir/$name.rho"

cert_status() {
    local i="$1"
    if [ $((i % 3)) -eq 2 ]; then
        printf 'stale'
    else
        printf 'valid'
    fi
}

{
    printf '!(import! &self rho)\n\n'
    printf '(= (certificate-proc-if $required (cert $task-tag $task-name $status $cert))\n'
    printf '   (if-equal $status $required\n'
    printf '     (rho.send $task-name (rho.send $cert rho.nil))\n'
    printf '     rho.nil))\n\n'
    printf '(= (auditor-proc (audit $task-tag $task-name $sink))\n'
    printf '   (rho.recv $task-name $msg (rho.send $sink (rho.drop $msg))))\n\n'
    printf '(= (build-certificates $required $certs)\n'
    printf '   (foldl-atom $certs rho.nil $acc $cert\n'
    printf '     (let $head (eval (certificate-proc-if $required $cert))\n'
    printf '       (rho.par $acc $head))))\n\n'
    printf '(= (build-auditors $auditors)\n'
    printf '   (foldl-atom $auditors rho.nil $acc $audit\n'
    printf '     (let $head (eval (auditor-proc $audit))\n'
    printf '       (rho.par $acc $head))))\n\n'
    printf '(= (certificate-quorum-program $required $certs $auditors)\n'
    printf '   (let $cert-procs (eval (build-certificates $required $certs))\n'
    printf '     (let $audit-procs (eval (build-auditors $auditors))\n'
    printf '       (rho.par $cert-procs $audit-procs))))\n\n'
    printf '!(let $program\n'
    printf '  (eval (certificate-quorum-program %s\n' "$required"
    printf '    ('
    for i in $(seq 0 $((n - 1))); do
        task=$((i % 4))
        status="$(cert_status "$i")"
        if [ "$i" -gt 0 ]; then
            printf '\n     '
        fi
        printf '(cert task%s $task%s %s $cert%s)' "$task" "$task" "$status" "$i"
    done
    printf ')\n'
    printf '    ('
    for i in $(seq 0 $((n - 1))); do
        task=$((i % 4))
        if [ "$i" -gt 0 ]; then
            printf '\n     '
        fi
        printf '(audit task%s $task%s $sink%s)' "$task" "$task" "$i"
    done
    printf ')))\n'
    printf '  $program)\n'
} > "$metta"

{
    printf '(rho:par\n'
    first=1
    for i in $(seq 0 $((n - 1))); do
        task=$((i % 4))
        printf '  (rho:recv $task%s $msg (rho:send $sink%s (rho:drop $msg)))\n' \
            "$task" "$i"
        first=0
    done
    for i in $(seq 0 $((n - 1))); do
        task=$((i % 4))
        status="$(cert_status "$i")"
        if [ "$status" != "$required" ]; then
            continue
        fi
        printf '  (rho:send $task%s (rho:send $cert%s rho:nil))\n' "$task" "$i"
        first=0
    done
    printf ')\n'
} > "$mrho"

first=1
{
    for i in $(seq 0 $((n - 1))); do
        task=$((i % 4))
        if [ "$first" -eq 0 ]; then
            printf ' |\n'
        fi
        first=0
        printf 'for (x%s <- @"task%s") { @"sink%s"!(*x%s) }' "$i" "$task" "$i" "$i"
    done
    for i in $(seq 0 $((n - 1))); do
        task=$((i % 4))
        status="$(cert_status "$i")"
        if [ "$status" != "$required" ]; then
            continue
        fi
        if [ "$first" -eq 0 ]; then
            printf ' |\n'
        fi
        first=0
        printf '@"task%s"!(@"cert%s"!(Nil))' "$task" "$i"
    done
    printf '\n'
} > "$rho"

printf '%s\t%s\t%s\t%s\n' "$name" "$metta" "$mrho" "$rho"
