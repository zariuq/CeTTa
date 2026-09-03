#!/usr/bin/env sh
set -eu

if [ "$#" -ne 3 ]; then
    echo "usage: $0 <Mettapedia-root> <langdef-compiler> <PeTTa-root>" >&2
    exit 2
fi

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mettapedia_root=$1
compiler=$2
petta_root=$3
source_file="$project_root/langdef/logic/tptp_ground_resolution_calculus_v1.metta"
runtime_file="$project_root/langdef/petta/finite_horn_runtime_v1.metta"
query_file="$project_root/tests/petta/tptp_ground_resolution_calculus_query_v1.metta"
program_file="$project_root/langdef/petta/generated/tptp_ground_resolution_calculus_v1.metta"
receipt_file="$project_root/langdef/petta/generated/tptp_ground_resolution_calculus_v1.receipt.metta"

if [ ! -x "$compiler" ]; then
    echo "langdef compiler is not executable: $compiler" >&2
    exit 2
fi
if [ ! -x "$petta_root/run.sh" ]; then
    echo "PeTTa run.sh is not executable under: $petta_root" >&2
    exit 2
fi

"$project_root/tools/check_tptp_ground_resolution_calculus_wire_v1.sh" \
    "$mettapedia_root"

shape=$(
    "$compiler" oracle-horn-shape --source "$source_file"
)
expected_shape='(FiniteHornStructuralShapeV1Summary 11 4 7 17 6 5 6 2 2 2 8 5 9cff63946a86ec178c28459190ba0b9bc7e329bad4569859d087fa5b2779bea5)'
if [ "$shape" != "$expected_shape" ]; then
    echo "unexpected ground-resolution finite-Horn shape: $shape" >&2
    exit 1
fi

"$compiler" oracle-petta-horn-check \
    --runtime "$runtime_file" \
    --source "$source_file" \
    --epilogue "$query_file" \
    --program "$program_file" \
    --receipt "$receipt_file" >/dev/null

output=$(cd "$petta_root" && timeout 30 sh run.sh --silent "$program_file")
expected_goal='((GroundResolve ground-resolution:positive-left p (ground-resolution:cons (ground-resolution:positive p) ground-resolution:nil) (ground-resolution:cons (ground-resolution:negative p) ground-resolution:nil) ground-resolution:nil))'
if ! printf '%s\n' "$output" | grep -Fqx "$expected_goal"; then
    echo "ground-resolution query did not produce the expected judgment" >&2
    printf '%s\n' "$output" >&2
    exit 1
fi

echo "PASS: authored ground-resolution calculus projects, admits, and executes through the generic finite-Horn runtime"
