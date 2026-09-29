#!/usr/bin/env bash
set -euo pipefail

runtime=${1:-./runtime/cetta-core.nogmp.json-gslt}
fixture=tests/langdef/tptp/host_symbol_readback_v1.metta

petta_output=$("$runtime" --quiet --lang petta "$fixture")
he_output=$("$runtime" --quiet --lang he "$fixture")
prime_output=$("$runtime" --quiet --lang prime "$fixture")

[[ "$petta_output" == $'true\n(HostSymbolReadbackV1 false true true false true false)' ]]
[[ "$he_output" == '[(HostSymbolReadbackV1 False True True False True True)]' ]]
[[ "$prime_output" == '[(HostSymbolReadbackV1 False True True False True True)]' ]]

printf '%s\n' 'TPTP host symbol readback: PeTTa, HE, Prime passed'
