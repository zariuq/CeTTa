#!/bin/sh
# Host capabilities in the direct compiler: a moded host relation gives a
# value back to the rules, a constant where its output goes fixes it, and an
# extern source lets two programs that share a host load together.
#   check_gslt_petta_capabilities_v1.sh COMPILER RUNTIME EVIDENCE_DIRECTORY
set -eu

if [ "$#" -ne 3 ]; then
    echo 'usage: check_gslt_petta_capabilities_v1.sh COMPILER RUNTIME EVIDENCE_DIRECTORY' >&2
    exit 2
fi
compiler=$1
runtime=$2
evidence=$3
test -x "$compiler" && test -x "$runtime"
mkdir -p "$evidence"
dir=tests/petta
host=$dir/gslt_capabilities_host_v1.metta
a=$dir/gslt_capabilities_a_v1.metta
b=$dir/gslt_capabilities_b_v1.metta
query=$dir/gslt_capabilities_query_v1.metta
expected=$dir/gslt_capabilities_v1.expected
sha256sum "$compiler" "$runtime" "$host" "$a" "$b" "$query" "$expected" \
    > "$evidence/inputs.sha256"

# NAME ARGUMENT...: the compiler refuses, saying why
refuse() {
    name=$1
    shift
    if "$compiler" petta-direct "$@" --out "$evidence/$name.metta" \
        > "$evidence/$name.txt" 2>&1; then
        echo "unexpected success in capability negative control: $name" >&2
        exit 1
    fi
}

# program A owns the host; program B links it
"$compiler" petta-direct --source "$a" --source "$host" \
    --entry-mode QuadA:10 --out "$evidence/a.metta" > "$evidence/a.txt"
"$compiler" petta-direct --source "$b" --extern "$host" \
    --entry-mode DoubleB:10 --entry-mode ZeroB:10 \
    --out "$evidence/b.metta" > "$evidence/b.txt"
test "$(grep -c '^(= (gslt:cap-double ' "$evidence/a.metta")" -eq 1
test "$(grep -c '^(: gslt:cap-double ' "$evidence/a.metta")" -eq 1
if grep -q '^(= (gslt:cap-\|^(: gslt:cap-\|^(= (gslt:oslf-external-relation\|^(: gslt:oslf-external-relation' \
    "$evidence/b.metta"; then
    echo 'program B renders a declaration or equation of its extern source' >&2
    exit 1
fi
CETTA_PETTA_SEARCH_MACHINE=1 "$runtime" --lang petta \
    "$evidence/a.metta" "$evidence/b.metta" "$query" \
    > "$evidence/cetta.txt" 2> "$evidence/cetta.stderr"
diff -u "$expected" "$evidence/cetta.txt"
test ! -s "$evidence/cetta.stderr"

# the control: B with the host as its own source renders the host's equations
# again, and beside A a host call answers twice
"$compiler" petta-direct --source "$b" --source "$host" \
    --entry-mode DoubleB:10 --entry-mode ZeroB:10 \
    --out "$evidence/b-own.metta" > "$evidence/b-own.txt"
CETTA_PETTA_SEARCH_MACHINE=1 "$runtime" --lang petta \
    "$evidence/a.metta" "$evidence/b-own.metta" "$query" \
    > "$evidence/cetta-own.txt" 2>&1
grep -qxF '((gslt:result:DoubleB:10 10) (gslt:result:DoubleB:10 10))' "$evidence/cetta-own.txt"

refuse extern-relation-rule --source "$host" --extern "$a" --entry-mode QuadA:10
grep -q 'declares host relations only' "$evidence/extern-relation-rule.txt"
refuse extern-c-target --source "$a" --extern "$host" --target c \
    --closed-entry-mode QuadA:10
grep -q 'no extern source' "$evidence/extern-c-target.txt"
refuse extern-native-types --source "$a" --extern "$host" \
    --native-types "$evidence/absent.metta" --closed-entry-mode QuadA:10
grep -q 'no extern source' "$evidence/extern-native-types.txt"
refuse undeclared-modes --source "$dir/gslt_capabilities_undeclared_modes_v1.metta" \
    --entry-mode UseV1:0
grep -q 'no declared capability' "$evidence/undeclared-modes.txt"
refuse twice-modes --source "$dir/gslt_capabilities_twice_modes_v1.metta" \
    --entry-mode UseV1:0
grep -q 'two mode declarations' "$evidence/twice-modes.txt"
refuse bad-digits --source "$dir/gslt_capabilities_bad_digits_v1.metta" \
    --entry-mode UseV1:0
grep -q 'a digit per argument' "$evidence/bad-digits.txt"
echo '(GsltPeTTaCapabilitiesV1Summary 5 6)'
