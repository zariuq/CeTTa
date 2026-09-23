#!/usr/bin/env bash
set -euo pipefail
if (( $# != 3 )); then
    echo 'usage: check_plain_bnf_handle_scope_v1.sh RUNTIME LIFETIME_OBSERVER EVIDENCE_DIRECTORY' >&2
    exit 2
fi
scope_runtime=$(realpath "$1")
scope_observer=$(realpath "$2")
mkdir -p "$3"
scope_evidence=$(realpath "$3")
scope_root=$(cd "$(dirname "$0")/../.." && pwd -P)
cd "$scope_root"
scope_fixture=tests/langdef/bnf/plain_bnf_handle_scope_v1.metta
test ! -e "$scope_evidence/inputs.sha256"
sha256sum "$scope_runtime" "$scope_observer" "$scope_fixture" "$0" \
    tests/support/test_plain_bnf_typed_lifetime_v1.c \
    src/atom.c src/binding/frame_identity.c src/atom.h src/native_handle.c src/native_handle.h \
    src/term_universe.c native/langdef_module.c lib/langdef.metta \
    langdef/bnf/plain_bnf_source_v1.metta \
    langdef/bnf/plain_bnf_parser_profile_v1.metta \
    langdef/bnf/plain_bnf_grammar_v1.metta \
    langdef/bnf/plain_bnf_cst_projection_v1.metta > "$scope_evidence/inputs.sha256"
env CETTA_PETTA_SEARCH_MACHINE=1 "$scope_observer" --lang petta "$scope_fixture" \
    > "$scope_evidence/observed.out" 2> "$scope_evidence/observed.err"
test ! -s "$scope_evidence/observed.err"
test "$(rg -c 'complete answers=0 live-before=0 live-after=0 opened=1 ' \
    "$scope_evidence/observed.out")" = 3
test "$(rg -c 'complete answers=2 live-before=0 live-after=1 opened=1 ' \
    "$scope_evidence/observed.out")" = 1
test "$(rg -Fxc '(BnfLifetimeOccurrenceV1 HandleScopeStoredV1)' \
    "$scope_evidence/observed.out")" = 2
rg -Fxq 'HandleScopeEscapedParserWorksV1' "$scope_evidence/observed.out"
rg -Fxq '(HandleScopeEarlyCloseV1 True)' "$scope_evidence/observed.out"
rg -Fxq '(HandleScopeSecondCloseV1 False)' "$scope_evidence/observed.out"
rg -Fxq '(BnfLifetimeContextTeardownV1 live-before=0 live-after=0 probes=4)' \
    "$scope_evidence/observed.out"
for scope_dialect in he prime; do
    "$scope_runtime" --lang "$scope_dialect" "$scope_fixture" \
        > "$scope_evidence/$scope_dialect.out" 2> "$scope_evidence/$scope_dialect.err"
    test ! -s "$scope_evidence/$scope_dialect.err"
    rg -Fxq '[HandleScopeStoredV1, HandleScopeStoredV1]' "$scope_evidence/$scope_dialect.out"
    rg -Fxq '[HandleScopeEscapedParserWorksV1]' "$scope_evidence/$scope_dialect.out"
    rg -Fxq '[(HandleScopeEarlyCloseV1 True)]' "$scope_evidence/$scope_dialect.out"
    rg -Fxq '[(HandleScopeSecondCloseV1 False)]' "$scope_evidence/$scope_dialect.out"
    if rg -q 'HandleScopeUnexpectedV1|\(Error ' "$scope_evidence/$scope_dialect.out"; then
        exit 1
    fi
done
sha256sum -c "$scope_evidence/inputs.sha256" > "$scope_evidence/inputs-after.txt"
printf '(PlainBnfHandleScopeV1 zero-result-kinds=3 duplicate-results=2 escape-lanes=3)\n' \
    | tee "$scope_evidence/summary.txt"
