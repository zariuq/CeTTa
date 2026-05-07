#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUNTIME_DIR="$ROOT/runtime"
STAMP="$(date +%Y%m%d-%H%M%S)"
ARTIFACT="$RUNTIME_DIR/cetta-$STAMP"
META="$ARTIFACT.meta"
BUILD_MODE="${BUILD:-python}"

cd "$ROOT"

ulimit -v 10485760
make -j1 BUILD="$BUILD_MODE" test
make -j1 BUILD="$BUILD_MODE" test-profiles
make -j1 BUILD="$BUILD_MODE" ENABLE_RUNTIME_STATS=1 test-runtime-stats
make -j1 BUILD="$BUILD_MODE" tail-recursion-check

mkdir -p "$RUNTIME_DIR"
cp "$ROOT/cetta" "$ARTIFACT"
chmod 755 "$ARTIFACT"
ln -sfn "$(basename "$ARTIFACT")" "$RUNTIME_DIR/cetta-current"

{
    printf 'artifact=%s\n' "$(basename "$ARTIFACT")"
    printf 'created_at=%s\n' "$(date -Iseconds)"
    printf 'build=%s\n' "$BUILD_MODE"
    printf 'verified_with=make -j1 BUILD=%s test; make -j1 BUILD=%s test-profiles; make -j1 BUILD=%s ENABLE_RUNTIME_STATS=1 test-runtime-stats; make -j1 BUILD=%s tail-recursion-check\n' \
        "$BUILD_MODE" "$BUILD_MODE" "$BUILD_MODE" "$BUILD_MODE"
    printf 'source_binary=%s\n' "$ROOT/cetta"
} > "$META"

ln -sfn "$(basename "$META")" "$RUNTIME_DIR/cetta-current.meta"

printf 'Promoted verified runtime: %s\n' "$ARTIFACT"
printf 'Stable path: %s\n' "$RUNTIME_DIR/cetta-current"
