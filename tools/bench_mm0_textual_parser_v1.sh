#!/usr/bin/env bash
set -euo pipefail

usage() {
    printf '%s\n' \
        'usage: bench_mm0_textual_parser_v1.sh --binary PATH --mm0-root PATH [--timeout SECONDS]'
}

binary=
mm0_root=
case_timeout=60

while (($#)); do
    case "$1" in
        --binary)
            (($# >= 2)) || { usage >&2; exit 2; }
            binary=$2
            shift 2
            ;;
        --mm0-root)
            (($# >= 2)) || { usage >&2; exit 2; }
            mm0_root=$2
            shift 2
            ;;
        --timeout)
            (($# >= 2)) || { usage >&2; exit 2; }
            case_timeout=$2
            shift 2
            ;;
        *)
            usage >&2
            exit 2
            ;;
    esac
done

[[ -n "$binary" && -x "$binary" && -n "$mm0_root" ]] || {
    usage >&2
    exit 2
}
[[ "$case_timeout" =~ ^[1-9][0-9]*$ ]] || {
    printf 'timeout must be a positive integer\n' >&2
    exit 2
}

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
generated="$root/langdef/mm0/generated"
pack="$generated/parser_pack_v1.abi"
lexical_nfa="$generated/lexical_nfa_v1.answers"
guard_nfa="$generated/guard_nfa_v1.answers"
guard_evidence="$generated/parser_guard_evidence_v1.abi"
guarded_nfa="$generated/guarded_nfa_v1.answers"
regular_compiler_digest=3fc737afb6df4e45dc19dd50597694297b4dd672965ef12197a0a64c492d9189
guarded_compiler_digest=395867dc43f7a202ec500faf1dfb28b281fd04e699a63a48d4d90f937204c181

for artifact in \
    "$pack" "$lexical_nfa" "$guard_nfa" "$guard_evidence" "$guarded_nfa"; do
    [[ -r "$artifact" ]] || {
        printf 'missing MM0 parser artifact: %s\n' "$artifact" >&2
        exit 2
    }
done

time_file=$(mktemp "$root/runtime/mm0-textual-parser-time.XXXXXX")
trap 'unlink "$time_file" 2>/dev/null || true' EXIT INT TERM

printf 'example\tbytes\tstatus\tgll\tglr\twall_seconds\tmaxrss_kib\n'

for name in unprovable string verifier hello miu goldbach set; do
    input="$mm0_root/examples/$name.mm0"
    [[ -r "$input" ]] || {
        printf 'missing MM0 example: %s\n' "$input" >&2
        exit 2
    }
    if LC_ALL=C grep -q '^import "' "$input"; then
        # `import` is an mm0-rs joining extension, not part of the MM0 grammar.
        # The benchmark intentionally measures only official, already joined
        # textual syntax.
        continue
    fi

    : >"$time_file"
    set +e
    output=$(
        /usr/bin/time -q -o "$time_file" -f '%e\t%M' \
            timeout "$case_timeout" "$binary" \
            "$pack" "$lexical_nfa" "$guard_nfa" "$guard_evidence" \
            "$guarded_nfa" "$input" \
            "$regular_compiler_digest" "$guarded_compiler_digest" 2>&1
    )
    status=$?
    set -e

    gll=$(awk -F '\t' '$1 == "gll-decision" { print $2 }' <<<"$output")
    glr=$(awk -F '\t' '$1 == "glr-decision" { print $2 }' <<<"$output")
    read -r wall_seconds maxrss_kib <"$time_file" || true
    wall_seconds=${wall_seconds:-unknown}
    maxrss_kib=${maxrss_kib:-unknown}

    if ((status == 124)); then
        decision_status=timeout
        gll=${gll:-pending}
        glr=${glr:-pending}
    elif ((status != 0)); then
        printf '%s\n' "$output" >&2
        exit "$status"
    elif [[ "$gll" != accepted || "$glr" != accepted ]]; then
        printf '%s\n' "$output" >&2
        printf 'completed MM0 example did not have agreeing accepted decisions: %s\n' \
            "$name" >&2
        exit 1
    else
        decision_status=complete
    fi

    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$name" "$(wc -c <"$input")" "$decision_status" \
        "$gll" "$glr" "$wall_seconds" "$maxrss_kib"
done
