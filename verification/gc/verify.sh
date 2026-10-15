#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    printf 'Usage: %s PATH_TO_BUILT_MATHLIB_PROJECT\n' "$0" >&2
    exit 2
fi

base=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
project=$(cd -- "$1" && pwd -P)
toolchain=$(cat -- "$project/lean-toolchain")
build="$base/.build"

python3 - "$base" <<'PY'
import hashlib
import json
import pathlib
import sys

base = pathlib.Path(sys.argv[1])
pins = json.loads((base / 'upstream-pins.json').read_text())
for entry in pins['files']:
    actual = hashlib.sha256((base / entry['vendored_path']).read_bytes()).hexdigest()
    if actual != entry['sha256']:
        raise SystemExit('Vendored source changed: ' + entry['vendored_path'])
print('Verified pinned upstream sources and license:', pins['commit'])
PY

if ! command -v elan >/dev/null; then
    printf 'Install elan and the toolchain specified by the dependency project.\n' >&2
    exit 2
fi

paths=("$build" "$base/upstream" "$base" "$project/.lake/build/lib/lean")
for library in "$project"/.lake/packages/*/.lake/build/lib/lean; do
    [[ ! -d "$library" ]] || paths+=("$library")
done
export LEAN_PATH
LEAN_PATH=$(IFS=:; printf '%s' "${paths[*]}")

mkdir -p -- "$build/Mettapedia/GSLT/Dynamics" "$build/Cetta/GC"
for module in StoreReachability StoreCollectionAdvanced; do
    elan run "$toolchain" lean --root="$base/upstream" \
        -o "$build/Mettapedia/GSLT/Dynamics/$module.olean" \
        "$base/upstream/Mettapedia/GSLT/Dynamics/$module.lean"
done
elan run "$toolchain" lean --root="$base" \
    -o "$build/Cetta/GC/SymbolCollection.olean" "$base/Cetta/GC/SymbolCollection.lean"
elan run "$toolchain" lean --root="$base" \
    -o "$build/Cetta/GC/SymbolLifetime.olean" "$base/Cetta/GC/SymbolLifetime.lean"
elan run "$toolchain" lean --root="$base" \
    -o "$build/Cetta/GC/SymbolRepresentation.olean" "$base/Cetta/GC/SymbolRepresentation.lean"
elan run "$toolchain" lean --root="$base" \
    -o "$build/Cetta/GC/SymbolHandles.olean" "$base/Cetta/GC/SymbolHandles.lean"
elan run "$toolchain" lean --root="$base" \
    -o "$build/Cetta/GC/ArchiveRegions.olean" "$base/Cetta/GC/ArchiveRegions.lean"
elan run "$toolchain" lean --root="$base" \
    -o "$build/Cetta/GC/ChoiceGenerations.olean" "$base/Cetta/GC/ChoiceGenerations.lean"
elan run "$toolchain" lean --root="$base" \
    -o "$build/Cetta/GC/RegisteredSymbols.olean" "$base/Cetta/GC/RegisteredSymbols.lean"
python3 - "$base" "$project" "$toolchain" <<'PY'
import hashlib
import json
import pathlib
import subprocess
import sys

base, project = map(pathlib.Path, sys.argv[1:3])
pins = json.loads((base / 'upstream-pins.json').read_text())
mathlib = project / '.lake/packages/mathlib'
mathlib_commit = subprocess.check_output(
    ['git', '-C', str(mathlib), 'rev-parse', 'HEAD'], text=True).strip()
result = {
    'status': 'passed',
    'source_commit': pins['commit'],
    'qualified_lean': sys.argv[3],
    'qualified_mathlib': mathlib_commit,
    'contract_sha256': hashlib.sha256(
        (base / 'Cetta/GC/SymbolCollection.lean').read_bytes()).hexdigest(),
    'lifetime_sha256': hashlib.sha256(
        (base / 'Cetta/GC/SymbolLifetime.lean').read_bytes()).hexdigest(),
    'representation_sha256': hashlib.sha256(
        (base / 'Cetta/GC/SymbolRepresentation.lean').read_bytes()).hexdigest(),
    'handles_sha256': hashlib.sha256(
        (base / 'Cetta/GC/SymbolHandles.lean').read_bytes()).hexdigest(),
    'regions_sha256': hashlib.sha256(
        (base / 'Cetta/GC/ArchiveRegions.lean').read_bytes()).hexdigest(),
    'registered_symbols_sha256': hashlib.sha256(
        (base / 'Cetta/GC/RegisteredSymbols.lean').read_bytes()).hexdigest(),
    'legacy_symbol_collection_enabled': False,
}
(base / '.build/verification-result.json').write_text(json.dumps(result, indent=2) + '\n')
print('Dependency revision:', mathlib_commit)
PY
printf 'GC symbol contract checked with %s.\n' "$toolchain"
