#!/usr/bin/env bash
set -euo pipefail

usage() {
    printf '%s\n' \
        'usage: test_mm0_textual_syntax_v1.sh --binary PATH --mm0-root PATH [--timeout SECONDS]'
}

binary=
mm0_root=
case_timeout=15

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

pack_source_digest=$(awk -F '\t' '$1 == "source-digest" { print $2; exit }' "$pack")
guard_source_digest=$(awk -F '\t' '$1 == "source-digest" { print $2; exit }' "$guard_evidence")
[[ -n "$pack_source_digest" && "$pack_source_digest" == "$guard_source_digest" ]] || {
    printf 'MM0 parser pack and guard evidence have different source identities\n' >&2
    exit 1
}
[[ $(awk -F '\t' '$1 == "closure" { print $2; exit }' "$pack") == partial ]] || {
    printf 'MM0 parser pack must expose its guarded partial closure\n' >&2
    exit 1
}
[[ $(grep -c '^derivation' "$guard_evidence") -eq 22 ]] || {
    printf 'MM0 parser guard evidence does not cover all 22 positive guards\n' >&2
    exit 1
}

run_case() {
    local input=$1
    local expected=$2
    local output status gll glr

    set +e
    output=$(timeout "$case_timeout" "$binary" \
        "$pack" "$lexical_nfa" "$guard_nfa" "$guard_evidence" \
        "$guarded_nfa" "$input" \
        "$regular_compiler_digest" "$guarded_compiler_digest" 2>&1)
    status=$?
    set -e
    if ((status != 0)); then
        printf '%s\n' "$output" >&2
        printf 'MM0 textual syntax case failed to execute: %s\n' "$input" >&2
        return 1
    fi

    gll=$(awk -F '\t' '$1 == "gll-decision" { print $2 }' <<<"$output")
    glr=$(awk -F '\t' '$1 == "glr-decision" { print $2 }' <<<"$output")
    if [[ "$gll" != "$expected" || "$glr" != "$expected" ]]; then
        printf '%s\n' "$output" >&2
        printf 'MM0 textual syntax disagreement for %s: expected %s\n' \
            "$input" "$expected" >&2
        return 1
    fi
}

run_semantic_discriminants() {
    local input="$root/tests/langdef/mm0/semantic_discriminants.mm0"
    local output status gll glr tag count

    set +e
    output=$(timeout "$case_timeout" "$binary" \
        "$pack" "$lexical_nfa" "$guard_nfa" "$guard_evidence" \
        "$guarded_nfa" "$input" \
        "$regular_compiler_digest" "$guarded_compiler_digest" 2>&1)
    status=$?
    set -e
    if ((status != 0)); then
        printf '%s\n' "$output" >&2
        printf 'MM0 semantic-discriminant case failed to execute\n' >&2
        return 1
    fi

    gll=$(awk -F '\t' '$1 == "gll-decision" { print $2 }' <<<"$output")
    glr=$(awk -F '\t' '$1 == "glr-decision" { print $2 }' <<<"$output")
    if [[ "$gll" != accepted || "$glr" != accepted ]]; then
        printf '%s\n' "$output" >&2
        printf 'MM0 semantic-discriminant parsers did not agree on acceptance\n' >&2
        return 1
    fi

    for tag in \
        mm0-axiom-kind mm0-theorem-kind \
        mm0-prefix-kind mm0-infixl-kind mm0-infixr-kind; do
        count=$(grep -o "(node $tag " <<<"$output" | wc -l)
        # The stream prints one semantic result for GLL and one for GLR.
        if [[ "$count" -ne 2 ]]; then
            printf '%s\n' "$output" >&2
            printf 'MM0 semantic AST did not preserve exactly one %s per parser\n' \
                "$tag" >&2
            return 1
        fi
    done
}

positive=0
negative=0
for input in "$mm0_root"/tests/mm0_mmu/pass/*.mm0; do
    run_case "$input" accepted
    ((positive += 1))
done
for input in "$mm0_root"/tests/mm0_mmu/fail/*.mm0; do
    run_case "$input" rejected
    ((negative += 1))
done

[[ $positive -eq 6 && $negative -eq 24 ]] || {
    printf 'unexpected MM0 upstream syntax corpus size: %s positive, %s negative\n' \
        "$positive" "$negative" >&2
    exit 1
}

run_semantic_discriminants

printf '(MM0TextualSyntaxV1Summary %s %s 22 5 0)\n' \
    "$positive" "$negative"
