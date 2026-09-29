#!/usr/bin/env bash
# Regenerate real PeTTa's function vocabulary from a PeTTa checkout: its
# registered functions (fun/1 after loading src/metta.pl), the special forms
# its translator dispatches on, and the heads lib_he defines.
set -euo pipefail
if (( $# != 2 )); then
    echo 'usage: petta_reference_vocabulary_v1.sh PETTA_CHECKOUT OUTPUT' >&2
    exit 2
fi
root=$(realpath "$1")
output=$2
test -f "$root/src/metta.pl" && test -f "$root/src/translator.pl" && test -f "$root/lib/lib_he.metta"
commit=$(git -C "$root" rev-parse --short HEAD)
# The vocabulary is named by its commit, so it is read from that commit alone.
git -C "$root" diff --quiet HEAD -- src lib || {
    echo "the PeTTa checkout has local changes under src/ or lib/; the vocabulary names commit $commit" >&2
    exit 1
}
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
cat > "$scratch/dump.pl" <<'EOF'
:- initialization(dump, main).
dump :- consult('src/metta.pl'), forall(fun(F), (write(F), nl)), halt.
EOF
(cd "$root" && swipl -q "$scratch/dump.pl" 2>/dev/null) | sort -u > "$scratch/functions"
test -s "$scratch/functions"
grep -oE "HV == '?[^' ,)]+'?" "$root/src/translator.pl" | sed -E "s/HV == '?//; s/'$//" | sort -u > "$scratch/forms"
grep -oE '^\(= \([^ )]+' "$root/lib/lib_he.metta" | sed 's/^(= (//' | sort -u > "$scratch/lib_he"
{
    echo "# Real PeTTa's function vocabulary at commit $commit: its registered"
    echo "# functions (fun/1 after loading src/metta.pl), the special forms its"
    echo "# translator dispatches on, and the heads lib_he defines.  Regenerate with"
    echo "# make refresh-petta-reference-vocabulary-v1 PETTA_REFERENCE_ROOT=<checkout>."
    echo "[functions]"; cat "$scratch/functions"
    echo "[forms]"; cat "$scratch/forms"
    echo "[lib_he]"; cat "$scratch/lib_he"
} > "$output"
