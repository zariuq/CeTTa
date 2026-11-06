#!/usr/bin/env bash
# The evaluator's reading on its fixtures: each FILE.mg must print FILE.items
# and fail with FILE.err (empty when Megalodon reads the file to the end), as
# Megalodon's own parser does.  The tokens (FILE.tokens) are the presentations'
# own reading, compared by test-megalodon-gslt.
set -euo pipefail
dump=${1:?usage: run_source_reader_fixtures.sh MG_SOURCE_DUMP}
dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/source_reader" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/mg-source-fixtures.XXXXXX")
cleanup() {
    rm -f -- "$work/items" "$work/err"
    rmdir -- "$work"
}
trap cleanup EXIT
pass=0
fail=0
for input in "$dir"/*.mg; do
    name=$(basename "$input" .mg)
    status=0
    "$dump" --items "$input" > "$work/items" 2> "$work/err" || status=$?
    expected_status=0
    [[ -s "$dir/$name.err" ]] && expected_status=1
    if cmp -s "$work/items" "$dir/$name.items" &&
       cmp -s "$work/err" "$dir/$name.err" &&
       ((status == expected_status)); then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        printf 'FAIL %s (exit %d, expected %d)\n' "$name" "$status" "$expected_status"
        diff "$dir/$name.items" "$work/items" | head -5 || true
        diff "$dir/$name.err" "$work/err" | head -5 || true
    fi
done
printf '(MegalodonSourceReaderFixtures %d passed %d failed)\n' "$pass" "$fail"
((fail == 0))
