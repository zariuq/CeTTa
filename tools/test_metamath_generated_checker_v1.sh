#!/bin/sh

set -eu

output=$(
    "${METAMATH_GENERATED_CHECKER:?METAMATH_GENERATED_CHECKER is required}" \
        "$1" --occurrence-fold
)

printf '%s\n' "$output" |
    grep -Fqx "$(printf 'outcome\taccepted')"
printf '%s\n' "$output" |
    grep -Fqx "$(printf 'indexed-inference-committed\t1')"
