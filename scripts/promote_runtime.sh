#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUNTIME_DIR="$ROOT/runtime"
STAMP="$(date +%Y%m%d-%H%M%S)"
ARTIFACT="$RUNTIME_DIR/cetta-$STAMP"
META="$ARTIFACT.meta"

cd "$ROOT"

ulimit -v 6291456
make test
make tail-recursion-check

mkdir -p "$RUNTIME_DIR"
cp "$ROOT/cetta" "$ARTIFACT"
chmod 755 "$ARTIFACT"
ln -sfn "$(basename "$ARTIFACT")" "$RUNTIME_DIR/cetta-current"

{
    printf 'artifact=%s\n' "$(basename "$ARTIFACT")"
    printf 'created_at=%s\n' "$(date -Iseconds)"
    printf 'verified_with=make test; make tail-recursion-check\n'
    printf 'source_binary=%s\n' "$ROOT/cetta"
} > "$META"

ln -sfn "$(basename "$META")" "$RUNTIME_DIR/cetta-current.meta"

printf 'Promoted verified runtime: %s\n' "$ARTIFACT"
printf 'Stable path: %s\n' "$RUNTIME_DIR/cetta-current"
