#!/bin/sh
set -eu

if [ "$#" -ne 3 ]; then
    echo "usage: check_gslt_petta_entries_v1.sh COMPILER RUNTIME EVIDENCE_DIRECTORY" >&2
    exit 2
fi
compiler=$1
runtime=$2
evidence=$3
mkdir -p "$evidence"
test -x "$compiler" && test -x "$runtime"
sha256sum "$compiler" "$runtime" > "$evidence/executables.sha256"
for kind in single multiple; do
    "$compiler" petta-direct \
        --source "tests/petta/gslt_entry_${kind}_v1.metta" \
        --closed-entry-mode EntryLookupV1:10 \
        --closed-entry-mode EntryLookupV1:10 \
        --out "$evidence/$kind.metta" > "$evidence/$kind-generation.txt"
    test "$(rg -c '^\(= \(gslt:entry:EntryLookupV1:10 ' "$evidence/$kind.metta")" -eq 1
    CETTA_PETTA_SEARCH_MACHINE=1 "$runtime" --lang petta \
        "$evidence/$kind.metta" tests/petta/gslt_entry_query_v1.metta \
        > "$evidence/$kind-actual.txt"
    test "$(wc -l < "$evidence/$kind-actual.txt")" -eq 2
    test "$(rg -F -x -c '()' "$evidence/$kind-actual.txt")" -eq 1
done
rg -F -x -q '((gslt:result:EntryLookupV1:10 EntryFirstV1))' "$evidence/single-actual.txt"
rg -F -x -q '((gslt:result:EntryLookupV1:10 EntryFirstV1) (gslt:result:EntryLookupV1:10 EntrySecondV1) (gslt:result:EntryLookupV1:10 EntryFirstV1))' "$evidence/multiple-actual.txt"
echo '(GsltPeTTaStableEntryV1Summary 6 0)'
