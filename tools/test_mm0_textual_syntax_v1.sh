#!/usr/bin/env bash
set -euo pipefail

usage() {
    printf '%s\n' \
        'usage: test_mm0_textual_syntax_v1.sh --binary PATH [--scaling | --show-file FILE | --mm0-root PATH] [--timeout SECONDS]'
}

binary=
mm0_root=
show_file=
scaling=0
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
        --show-file)
            (($# >= 2)) || { usage >&2; exit 2; }
            show_file=$2
            shift 2
            ;;
        --scaling)
            scaling=1
            shift
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

[[ -n "$binary" && -x "$binary" ]] || {
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

run_parser() {
    timeout "$case_timeout" "$binary" \
        "$pack" "$lexical_nfa" "$guard_nfa" "$guard_evidence" \
        "$guarded_nfa" "$1" "$regular_compiler_digest" "$guarded_compiler_digest"
}

run_case() {
    local input=$1
    local expected=$2
    local output status gll glr

    set +e
    output=$(run_parser "$input" 2>&1)
    status=$?
    set -e
    if ((status != 0)); then
        printf '%s\n' "$output" >&2
        printf 'MM0 textual syntax case failed to execute: %s\n' "$input" >&2
        return 1
    fi

    gll=$(awk -F '\t' '$1 == "gll-decision" { print $2 }' <<<"$output")
    glr=$(awk -F '\t' '$1 == "glr-decision" { print $2 }' <<<"$output")
    if [[ "$gll" != "$expected" || "$glr" != "$expected" ]] ||
        ! grep -Fxq $'gll-outcome\tcompleted' <<<"$output" ||
        ! grep -Fxq $'glr-outcome\tcompleted' <<<"$output"; then
        printf '%s\n' "$output" >&2
        printf 'MM0 textual syntax disagreement for %s: expected %s\n' \
            "$input" "$expected" >&2
        return 1
    fi
    if [[ "${3:-}" == work ]]; then
        awk -F '\t' '$1 == "final-parse-work-items" { print $2 }' <<<"$output"
    fi
}

run_semantic_discriminants() {
    local input="$root/tests/langdef/mm0/semantic_discriminants.mm0"
    local output status gll glr tag count

    set +e
    output=$(run_parser "$input" 2>&1)
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

if [[ -n "$show_file" ]]; then
    run_parser "$show_file" | awk -F '\t' '
        $1 ~ /^(gll|glr)-(outcome|decision|result)$/ || $1 == "final-parse-work-items"'
    exit
fi

if ((scaling)); then
    mkdir -p "$root/runtime/qualification"
    input=$(mktemp "$root/runtime/qualification/mm0-parser-scaling.XXXXXX")
    trap 'rm -f "$input"' EXIT
    work=()
    for size in 4 8 16; do
        for ((i = 0; i < size; ++i)); do
            printf 'sort s%04d;\n' "$i"
        done >"$input"
        count=$(run_case "$input" accepted work)
        [[ "$count" =~ ^[0-9]+$ && "$count" -gt 0 ]] || {
            echo 'MM0 parser did not report its work count' >&2
            exit 1
        }
        work+=("$count")
    done
    printf 'MM0 parser work items for 4, 8, 16 declarations: %s\n' "${work[*]}"
    # Distinct top-level declarations have a fixed-size grammar per item.
    # A 20% allowance leaves room for batching; quadratic growth fails.
    ((work[0] < work[1] && work[1] < work[2] &&
      10 * (work[2] - work[1]) <= 24 * (work[1] - work[0])))
    exit
fi

if [[ -z "$mm0_root" ]]; then
    run_semantic_discriminants
    run_case "$root/tests/langdef/mm0/textual_missing_terminator.mm0" rejected
    printf '%s\n' '(MM0TextualSyntaxV1Summary 1 1 22 5 0)'
    exit
fi

# The upstream groups classify full verification, not the parser alone.
mkdir -p "$root/runtime/qualification"
corpus=$(mktemp "$root/runtime/qualification/mm0-syntax-cases.XXXXXX")
trap 'rm -f "$corpus"' EXIT
python3 "$root/tools/mm0_textual_corpus_v1.py" --mm0-root "$mm0_root" >"$corpus"
positive=0
negative=0
while IFS= read -r -d '' input && IFS= read -r -d '' expected; do
    run_case "$input" "$expected"
    if [[ "$expected" == accepted ]]; then
        ((positive += 1))
    else
        ((negative += 1))
    fi
done <"$corpus"
((positive + negative > 0)) || {
    printf 'MM0 syntax corpus is empty\n' >&2
    exit 1
}

run_semantic_discriminants

printf '(MM0TextualSyntaxV1Summary %s %s 22 5 0)\n' \
    "$positive" "$negative"
