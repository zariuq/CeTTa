#!/usr/bin/env bash
# Deliberately alter private copies, leaving the retained result untouched.
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
CAPSULE=$(realpath "${1:?usage: check_capsule.sh CAPSULE}")
CONTROL=$(mktemp -d "$CAPSULE/integrity-control.XXXXXX")
cp "$CAPSULE/PROFILE.txt" "$CAPSULE/PROFILE.sha256" "$CAPSULE/ARTIFACTS.sha256" "$CONTROL/"
ln -s "$CAPSULE/profile" "$CONTROL/profile"
for artifact in A0.sic A1.sic A2.sic A2.dec original.normalized.metta A1.rewritten.metta A2.rewritten.metta A2.helpers.metta; do
    cp "$CAPSULE/$artifact" "$CONTROL/$artifact"
done
(cd "$CONTROL"; sha256sum --strict --status -c PROFILE.sha256; sha256sum --strict --status -c ARTIFACTS.sha256)
printf '\nchanged profile metadata\n' >> "$CONTROL/PROFILE.txt"
if bash "$HERE/replay.sh" "$CONTROL" > "$CONTROL/profile.stdout" 2> "$CONTROL/profile.stderr"; then
    printf 'FAIL: changed profile metadata was accepted\n' >&2
    exit 1
fi
cp "$CAPSULE/PROFILE.txt" "$CONTROL/PROFILE.txt"
printf ' 0\n' >> "$CONTROL/A2.dec"
if bash "$HERE/replay.sh" "$CONTROL" > "$CONTROL/artifact.stdout" 2> "$CONTROL/artifact.stderr"; then
    printf 'FAIL: changed archive was accepted\n' >&2
    exit 1
fi
printf 'PASS: changed profile metadata and changed archive refused before decoding\n'
