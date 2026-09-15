#!/bin/sh
set -eu

if [ "$#" -lt 3 ] || [ "$#" -gt 4 ]; then
    echo 'usage: check_gslt_petta_static_calls_v1.sh COMPILER RUNTIME EVIDENCE_DIRECTORY [SWI_PETTA_SOURCE]' >&2
    exit 2
fi
compiler=$1
runtime=$2
evidence=$3
test -x "$compiler" && test -x "$runtime"
mkdir -p "$evidence"
test ! -e "$evidence/program.metta"
source=tests/petta/gslt_static_calls_v1.metta
query=tests/petta/gslt_static_calls_query_v1.metta
expected=tests/petta/gslt_static_calls_v1.expected
sha256sum "$compiler" "$runtime" "$source" "$query" "$expected" \
    > "$evidence/inputs.sha256"
"$compiler" petta-direct --source "$source" \
    --closed-entry-mode CopyUseV1:0 \
    --closed-entry-mode CopyListV1:10 \
    --closed-entry-mode ChoiceUseV1:0 \
    --out "$evidence/program.metta" > "$evidence/generation.txt"
CETTA_PETTA_SEARCH_MACHINE=1 "$runtime" --lang petta \
    "$evidence/program.metta" "$query" \
    > "$evidence/cetta.txt" 2> "$evidence/cetta.stderr"
diff -u "$expected" "$evidence/cetta.txt"
test ! -s "$evidence/cetta.stderr"
if [ "$#" -eq 4 ]; then
    petta_source=$4
    test -f "$petta_source"
    sha256sum "$petta_source" "$(dirname "$petta_source")/translator.pl" \
        "$(dirname "$petta_source")/filereader.pl" \
        > "$evidence/swi-source.sha256"
    swipl -q -s "$petta_source" \
        -g 'current_prolog_flag(argv, [_,Program,Query]), load_metta_file(Program, _), load_metta_file(Query, R), maplist(swrite,R,S), maplist(writeln,S), halt' \
        -- --silent "$evidence/program.metta" "$query" \
        > "$evidence/swi.txt" 2> "$evidence/swi.stderr"
    diff -u "$expected" "$evidence/swi.txt"
    test ! -s "$evidence/swi.stderr"
    echo '(GsltPeTTaStaticCallsOracleV1Summary 4 0)'
fi
echo '(GsltPeTTaStaticCallsV1Summary 4 0)'
