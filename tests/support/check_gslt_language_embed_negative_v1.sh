#!/usr/bin/env bash
set -euo pipefail
if (( $# != 2 )); then
    echo 'usage: check_gslt_language_embed_negative_v1.sh GENERATOR NEW_EVIDENCE_DIRECTORY' >&2
    exit 2
fi
tool=$(realpath "$1")
repo=$(cd "$(dirname "$0")/../.." && pwd -P)
mkdir -p "$2"
evidence=$(realpath "$2")
test ! -e "$evidence/inputs.sha256"
cd "$repo"
fixtures=tests/support/gslt_language_embed_v1
sha256sum "$tool" "$0" "$fixtures/manifest.metta" "$fixtures/semantics.metta" > "$evidence/inputs.sha256"
mkdir "$evidence/good"
cp "$fixtures/manifest.metta" "$fixtures/semantics.metta" "$evidence/good/"
generate() {
    "$tool" --manifest "$1/manifest.metta" --source-root "$1" \
        --symbol wire_canary --header "$1/out.h" --source "$1/out.c" --header-include out.h
}
generate "$evidence/good"
# Independently specified historical pair/2 CGP1 wire vector, not a timing or implementation counter.
grep -q 'ca23b221d503e538a0d668dc448284014c2d5db30258c3fc5399e6e6fb7cea79' "$evidence/good/out.c"
checks=1
for mutation in undeclared duplicate equation anonymous overflow null-string malformed; do
    dir="$evidence/$mutation"
    mkdir "$dir"
    cp "$fixtures/manifest.metta" "$dir/manifest.metta"
    case "$mutation" in
        undeclared) pattern='s/(operator pair 2)/(operator pair 3)/' ;;
        duplicate) pattern='s/(operator pair 2)/(operator pair 2) (operator pair 2)/' ;;
        equation) pattern='s/(equations)/(equations (same a b))/' ;;
        anonymous) pattern='s/?x/?_/' ;;
        overflow) pattern='s/?x/9223372036854775808/' ;;
        null-string) pattern='s/?x/"\\u0000"/' ;;
        malformed) pattern='s/(head (pair ?x ?y))/(head pair)/' ;;
    esac
    sed "$pattern" "$fixtures/semantics.metta" > "$dir/semantics.metta"
    cp "$evidence/good/out.h" "$dir/out.h"
    cp "$evidence/good/out.c" "$dir/out.c"
    if generate "$dir" > "$dir/stdout" 2> "$dir/stderr"; then
        echo "FAIL: accepted $mutation" >&2; exit 1
    fi
    grep -q '^error:' "$dir/stderr"
    cmp "$dir/out.h" "$evidence/good/out.h"
    cmp "$dir/out.c" "$evidence/good/out.c"
    checks=$((checks + 1))
done
# Realpath containment must reject a symlink escape, not merely a '..' spelling.
mkdir "$evidence/escape"
cp "$fixtures/manifest.metta" "$evidence/escape/manifest.metta"
ln -s "$evidence/good/semantics.metta" "$evidence/escape/semantics.metta"
if generate "$evidence/escape" > "$evidence/escape/stdout" 2> "$evidence/escape/stderr"; then
    echo 'FAIL: accepted source symlink escape' >&2; exit 1
fi
grep -q 'outside root' "$evidence/escape/stderr"
checks=$((checks + 1))
if "$tool" --manifest "$evidence/good/manifest.metta" --symbol wire_canary \
    --header "$evidence/good/semantics.metta" --source "$evidence/good/out.c" \
    --header-include out.h > "$evidence/alias.stdout" 2> "$evidence/alias.stderr"; then
    echo 'FAIL: accepted output alias of semantic input' >&2; exit 1
fi
cmp "$evidence/good/semantics.metta" "$fixtures/semantics.metta"
checks=$((checks + 1))
sha256sum -c "$evidence/inputs.sha256" > "$evidence/inputs-after.txt"
echo "GsltLanguageNativeEmbedControlsV1: $checks checks, 0 failures"
