#!/usr/bin/env bash

set -euo pipefail

: "${CETTA_DIRECT_PETTA_DRIVER:?CETTA_DIRECT_PETTA_DRIVER is required}"
: "${CETTA_DIRECT_PETTA_TEST_ROOT:?CETTA_DIRECT_PETTA_TEST_ROOT is required}"
: "${CETTA_DIRECT_PETTA_RESULTS:?CETTA_DIRECT_PETTA_RESULTS is required}"

if [[ "$#" -ne 0 ]]; then
    echo "usage: $0" >&2
    exit 2
fi

driver=$(realpath "$CETTA_DIRECT_PETTA_DRIVER") || exit 2
test_root=$(realpath "$CETTA_DIRECT_PETTA_TEST_ROOT") || exit 2
suite="$test_root/run-testsuite-all"
tests_root="$test_root/tests"
results_dir=$(realpath "$(dirname "$CETTA_DIRECT_PETTA_RESULTS")") || exit 2
results="$results_dir/$(basename "$CETTA_DIRECT_PETTA_RESULTS")"
limit=${CETTA_DIRECT_PETTA_CORPUS_LIMIT:-0}
filter=${CETTA_DIRECT_PETTA_CORPUS_FILTER:-}

if [[ ! -x "$driver" || ! -f "$suite" || ! -d "$tests_root" ||
        ! "$limit" =~ ^[0-9]+$ ]]; then
    echo "direct PeTTa corpus driver received an invalid resource" >&2
    exit 2
fi

printf 'expected\tclass\tcase\toutcome\n' >"$results"

total=0
expected_pass=0
expected_fail=0
accepted=0
refused=0
parser_refused=0
parser_incomplete=0
missing=0
disagreement=0
harness=0

while IFS=$'\t' read -r expected relative; do
    if [[ -n "$filter" ]] && ! grep -Eq "$filter" <<<"$relative"; then
        continue
    fi
    if [[ "$limit" -ne 0 && "$total" -ge "$limit" ]]; then
        break
    fi
    total=$((total + 1))
    case "$expected" in
        pass) expected_pass=$((expected_pass + 1)) ;;
        fail) expected_fail=$((expected_fail + 1)) ;;
        *)
            echo "invalid corpus expectation: $expected" >&2
            exit 2
            ;;
    esac

    source_file="$tests_root/$relative"
    if [[ ! -f "$source_file" ]]; then
        class=harness
        outcome='missing corpus source'
        harness=$((harness + 1))
    else
        set +e
        outcome=$("$driver" "$source_file" 2>&1)
        status=$?
        set -e
        if [[ "$status" -ne 0 ]]; then
            class=harness
            harness=$((harness + 1))
        else
            case "$outcome" in
                '(MetamathDirectPeTTaOutcomeV1 AcceptedV1)')
                    class=accepted
                    accepted=$((accepted + 1))
                    ;;
                '(MetamathDirectPeTTaOutcomeV1 ParserRefusedV1)')
                    class=parser-refused
                    parser_refused=$((parser_refused + 1))
                    ;;
                '(MetamathDirectPeTTaOutcomeV1 ParserIncompleteV1)')
                    class=parser-incomplete
                    parser_incomplete=$((parser_incomplete + 1))
                    ;;
                '(MetamathDirectPeTTaOutcomeV1 MissingSemanticOutcomeV1)')
                    class=missing
                    missing=$((missing + 1))
                    ;;
                '(MetamathDirectPeTTaOutcomeV1 (RefusedV1 '*)
                    class=refused
                    refused=$((refused + 1))
                    ;;
                *)
                    class=harness
                    harness=$((harness + 1))
                    ;;
            esac
        fi
    fi

    if [[ ("$expected" == pass && "$class" == accepted) ||
          ("$expected" == fail &&
           ("$class" == refused || "$class" == parser-refused)) ]]; then
        :
    elif [[ "$class" != missing && "$class" != parser-incomplete &&
            "$class" != harness ]]; then
        disagreement=$((disagreement + 1))
    fi

    outcome=${outcome//$'\t'/ }
    outcome=${outcome//$'\n'/ }
    printf '%s\t%s\t%s\t%s\n' \
        "$expected" "$class" "$relative" "$outcome" >>"$results"
    printf 'direct-petta-corpus %u %s %s %s\n' \
        "$total" "$expected" "$class" "$relative" >&2
done < <(
    awk '$1 == "pass" || $1 == "fail" {
        gsub(/\\/, "", $2)
        print $1 "\t" $2
    }' "$suite"
)

printf '%s\n' \
    "(MetamathDirectPeTTaCorpusV1Summary $total $expected_pass $expected_fail $accepted $refused $parser_refused $parser_incomplete $missing $disagreement $harness)"
