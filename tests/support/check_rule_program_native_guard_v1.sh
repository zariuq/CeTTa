#!/usr/bin/env bash
set -euo pipefail
if (( $# != 1 )); then
    echo 'usage: check_rule_program_native_guard_v1.sh EVIDENCE_DIRECTORY' >&2
    exit 2
fi
guard_script=$(realpath "$0")
guard_repo=$(cd "$(dirname "$guard_script")/../.." && pwd -P)
mkdir -p "$1"
guard_evidence=$(realpath "$1")
test ! -e "$guard_evidence/inputs.sha256"
cd "$guard_repo"
guard_unit=tests/support/test_rule_program_native_guard_v1.c
guard_sources=(src/atom.c src/symbol.c src/name_key.c src/match.c
    src/prime_need.c src/variant_shape.c src/term_universe.c src/term_canon.c)
# This unit consumes the retained generated instructions. Source regeneration
# is a separate gate; this test does not claim to exercise its generator.
sha256sum "$guard_script" "$guard_unit" src/rule_machine.c src/rule_machine.h \
    src/generated/rule_machine_program_v1.generated.h \
    src/generated/cetta_execution_contracts.generated.h \
    "${guard_sources[@]}" > "$guard_evidence/inputs.sha256"
guard_cc=${CC:-cc}
read -r -a guard_flags <<< "${CFLAGS:--O2 -Wall -Werror}"
guard_flags+=(-std=c11 -g -pthread -ffunction-sections -fdata-sections -Isrc -I.
    -DCETTA_BUILD_WITH_GMP=0 -DCETTA_BUILD_WITH_RUNTIME_STATS=0)
guard_command=("$guard_cc" "${guard_flags[@]}" "$guard_unit" "${guard_sources[@]}"
    "-Wl,--gc-sections" -lm -o "$guard_evidence/test")
printf '%q ' "${guard_command[@]}" > "$guard_evidence/compile.command"
printf '\n' >> "$guard_evidence/compile.command"
"${guard_command[@]}" > "$guard_evidence/compile.log" 2>&1
"$guard_evidence/test" > "$guard_evidence/test.log" 2>&1
sha256sum "$guard_evidence/test" > "$guard_evidence/executable.sha256"
sed -n '1,20p' "$guard_evidence/test.log"
