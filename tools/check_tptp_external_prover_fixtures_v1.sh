#!/usr/bin/env sh
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <eprover> <vampire>" >&2
    exit 2
fi

project_root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
eprover=$1
vampire=$2
problem_relative=examples/tptp/external_ground_refutation.p
checked_e="$project_root/tests/langdef/tptp/e_ground_refutation.tstp"
checked_vampire="$project_root/tests/langdef/tptp/vampire_ground_refutation.tstp"
generated_dir="$project_root/runtime/bootstrap"

if [ ! -x "$eprover" ]; then
    echo "E prover is not executable: $eprover" >&2
    exit 2
fi
if [ ! -x "$vampire" ]; then
    echo "Vampire is not executable: $vampire" >&2
    exit 2
fi
if [ ! -f "$project_root/$problem_relative" ]; then
    echo "TPTP problem is missing: $problem_relative" >&2
    exit 2
fi

mkdir -p "$generated_dir"
work=$(mktemp -d "$generated_dir/tptp-external-prover-fixtures.XXXXXX")
trap 'rm -rf "$work"' EXIT INT TERM

(cd "$project_root" &&
    "$eprover" --tstp-out --proof-object=1 --auto "$problem_relative") \
    >"$work/e.raw" 2>&1
if ! grep -Fq '% SZS status Unsatisfiable' "$work/e.raw"; then
    echo "E did not report an unsatisfiable result" >&2
    cat "$work/e.raw" >&2
    exit 1
fi
awk '
  /^% SZS output start CNFRefutation$/ { inside = 1; next }
  /^% SZS output end CNFRefutation$/ { inside = 0 }
  inside { print }
' "$work/e.raw" >"$work/e.tstp"

(cd "$project_root" &&
    "$vampire" --mode casc --proof tptp "$problem_relative") \
    >"$work/vampire.raw" 2>&1
if ! grep -Fq '% SZS status Unsatisfiable' "$work/vampire.raw"; then
    echo "Vampire did not report an unsatisfiable result" >&2
    cat "$work/vampire.raw" >&2
    exit 1
fi
awk '
  /^% SZS output start Proof / { inside = 1; next }
  /^% SZS output end Proof / { inside = 0 }
  inside { print }
' "$work/vampire.raw" >"$work/vampire.tstp"

if ! cmp -s "$work/e.tstp" "$checked_e"; then
    echo "E TSTP output differs from the checked fixture" >&2
    exit 1
fi
if ! cmp -s "$work/vampire.tstp" "$checked_vampire"; then
    echo "Vampire TSTP output differs from the checked fixture" >&2
    exit 1
fi

echo "PASS: checked E and Vampire TSTP fixtures regenerate exactly"
