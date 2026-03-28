#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

no_name_files=(
  src/atom.c
  src/match.c
  src/eval.c
  src/space.c
  src/parser.c
  src/subst_tree.c
  src/space_match_backend.c
  src/compile.c
  src/grounded.c
  src/native_handle.c
  src/cetta_stdlib.c
  native/metamath/module.c
)

atom_match_files=(
  src/atom.c
  src/match.c
)

builtin_semantic_files=(
  src/eval.c
  src/space.c
  src/compile.c
  src/match.c
  src/grounded.c
)

eval_symbolid_files=(
  src/eval.c
)

name_hits="$(
  rg -n -- '->name\b' "${no_name_files[@]}" || true
)"
name_hits="$(
  printf '%s\n' "$name_hits" | rg -v 'profile->name|ops->name' || true
)"
if [ -n "$name_hits" ]; then
  printf '%s\n' "$name_hits"
  echo "SymbolId guard failed: raw Atom->name access remains in guarded files." >&2
  exit 1
fi

if rg -n 'strcmp\(' "${atom_match_files[@]}" | rg -v 'ground\.sval'; then
  echo "SymbolId guard failed: unexpected strcmp in atom/match hot paths." >&2
  exit 1
fi

builtin_hits="$(
  rg -n 'expr_head_is\([^)]*"search-policy"|atom_is_symbol\([^)]*"(True|False|=|->|:|Atom|%Undefined%|Variable|Bindings|capture|function)"' \
    "${builtin_semantic_files[@]}" || true
)"
if [ -n "$builtin_hits" ]; then
  printf '%s\n' "$builtin_hits"
  echo "SymbolId guard failed: semantic builtin checks regressed to text in guarded files." >&2
  exit 1
fi

eval_regressions="$(
  rg -n 'expr_head_is\(|atom_is_symbol\(|registry_(bind|lookup)\(' \
    "${eval_symbolid_files[@]}" || true
)"
if [ -n "$eval_regressions" ]; then
  printf '%s\n' "$eval_regressions"
  echo "SymbolId guard failed: eval.c regressed to text/head or text-keyed registry dispatch." >&2
  exit 1
fi

echo "PASS: symbolid guard"
