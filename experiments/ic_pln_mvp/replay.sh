#!/usr/bin/env bash
# Decode a retained experiment capsule, never a bare archive under stock LC2.
set -euo pipefail
CAPSULE=$(realpath "${1:?usage: replay.sh CAPSULE}")
cd "$CAPSULE"
test "$(head -n 1 PROFILE.txt)" = 'IC-PLN-FROZEN-SAFE-1'
sha256sum --strict --status -c PROFILE.sha256
sha256sum --strict --status -c ARTIFACTS.sha256
OUT=$(mktemp -d "$CAPSULE/replay.XXXXXX")
quote() {
    local value=$1
    value=${value//\\/\\\\}; value=${value//\"/\\\"}
    value=${value//$'\n'/\\n}; value=${value//$'\r'/\\r}; value=${value//$'\t'/\\t}
    printf '"%s"' "$value"
}
{
    printf '!(import! &self %s)\n' "$(quote "$CAPSULE/profile/poc.metta")"
    printf '!(pln-poc:decode %s %s)\n' "$(quote "$CAPSULE/A2.dec")" "$(quote "$OUT/restored.metta")"
} > "$OUT/decode.metta"
profile/cetta --lang petta "$OUT/decode.metta" > "$OUT/decode.stdout" 2> "$OUT/decode.stderr"
test ! -s "$OUT/decode.stderr"
grep -q '^[(]PLN:Decoded)' "$OUT/decode.stdout"
cmp original.normalized.metta "$OUT/restored.metta"
printf 'PASS: frozen profile verified; fresh-process reconstruction agrees\n'
