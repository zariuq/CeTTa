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
name="job_queue_collector_$n"
metta="$out_dir/$name.metta"
mrho="$out_dir/$name.mrho"
rho="$out_dir/$name.rho"

emit_collector() {
    local style="$1"
    python3 - "$n" "$style" <<'PY'
import sys

n = int(sys.argv[1])
style = sys.argv[2]

if style == "metta":
    send = lambda i: f"  (rho.send $out{i} (rho.drop $r{i}))"
    wrap = lambda i, body: f"(rho.recv $done{i} $r{i}\n{body})"
    nil = "  rho.nil"
    head = "(rho.par"
elif style == "mrho":
    send = lambda i: f"  (rho:send $out{i} (rho:drop $r{i}))"
    wrap = lambda i, body: f"(rho:recv $done{i} $r{i}\n{body})"
    nil = "  rho:nil"
    head = "(rho:par"
else:
    raise SystemExit(f"unsupported collector style: {style}")

body = "\n".join([head] + [send(i) for i in range(n)] + [nil + ")"])
for i in range(n - 1, -1, -1):
    body = wrap(i, body)
print(body)
PY
}

{
    printf '!(import! &self rho)\n\n'
    printf '(= (queue-job $jobs $payload)\n'
    printf '   (rho.send $jobs (rho.send $payload rho.nil)))\n\n'
    printf '(= (queue-worker $jobs $done)\n'
    printf '   (rho.recv $jobs $job\n'
    printf '     (rho.send $done (rho.drop $job))))\n\n'
    printf '!(let $program\n'
    printf '  (eval\n'
    printf '    (rho.par\n'
    for i in $(seq 0 $((n - 1))); do
        printf '      (queue-worker $jobs $done%s)\n' "$i"
    done
    for i in $(seq 0 $((n - 1))); do
        printf '      (queue-job $jobs $payload%s)\n' "$i"
    done
    emit_collector metta | sed 's/^/      /'
    printf '))\n'
    printf '  $program)\n'
} > "$metta"

{
    printf '(rho:par\n'
    for i in $(seq 0 $((n - 1))); do
        printf '  (rho:recv $jobs $job (rho:send $done%s (rho:drop $job)))\n' "$i"
    done
    for i in $(seq 0 $((n - 1))); do
        printf '  (rho:send $jobs (rho:send $payload%s rho:nil))\n' "$i"
    done
    emit_collector mrho | sed 's/^/  /'
    printf ')\n'
} > "$mrho"

{
    for i in $(seq 0 $((n - 1))); do
        if [ "$i" -gt 0 ]; then
            printf ' |\n'
        fi
        printf 'for (job <- @"jobs") { @"done%s"!(*job) }' "$i"
    done
    for i in $(seq 0 $((n - 1))); do
        printf ' |\n@"jobs"!(@"payload%s"!(Nil))' "$i"
    done
    printf ' |\n'
    for i in $(seq $(($n - 1)) -1 0); do
        printf 'for (r%s <- @"done%s") { ' "$i" "$i"
    done
    first=1
    for i in $(seq 0 $((n - 1))); do
        if [ "$first" -eq 0 ]; then
            printf ' |\n'
        fi
        first=0
        printf '@"out%s"!(*r%s)' "$i" "$i"
    done
    printf ' '
    for _ in $(seq 0 $((n - 1))); do
        printf '}'
    done
    printf '\n'
} > "$rho"

printf '%s\t%s\t%s\t%s\n' "$name" "$metta" "$mrho" "$rho"
