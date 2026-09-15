#!/bin/sh
set -eu

if [ "$#" -lt 3 ] || [ "$#" -gt 6 ]; then
    echo 'usage: run_collector_comparison.sh RUNTIME GENERATED_COMPOSITION EVIDENCE_DIRECTORY [RULE_COUNTS] [TRIALS] [SECONDS_PER_RUN]' >&2
    exit 2
fi
runtime=$(realpath "$1")
program=$(realpath "$2")
evidence=$3
counts=${4:-'25 50 100 200 1000'}
trials=${5:-3}
deadline=${6:-60}
for number in $counts "$trials" "$deadline"; do
    case $number in ''|*[!0-9]*|0) echo "invalid positive integer: $number" >&2; exit 2;; esac
done
test -x "$runtime" && test -f "$program"
mkdir -p "$evidence"
evidence=$(realpath "$evidence")
test ! -e "$evidence/results.tsv"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror \
    benchmarks/bnf/generate_chain.c -o "$evidence/generate-chain"
sha256sum "$runtime" "$program" lib/lib_bnf.metta langdef/bnf/*.metta \
    benchmarks/bnf/generate_chain.c benchmarks/bnf/run_collector_comparison.sh \
    > "$evidence/inputs.sha256"
git rev-parse HEAD > "$evidence/base-commit.txt"
git diff --cached | sha256sum > "$evidence/staged-diff.sha256"
printf 'trial\trules\torder\tphase\tcollector\tstatus\telapsed_s\tpeak_rss_kib\texit_code\n' > "$evidence/results.tsv"
trial=1
pair=0
while [ "$trial" -le "$trials" ]; do
    for count in $counts; do
        for order in forward reverse; do
            for phase in declarations validate; do
                # Interleave paired implementations; do not run all baselines first.
                case $(((trial + pair) % 2)) in
                    0) routes='indexed linear';;
                    1) routes='linear indexed';;
                esac
                for route in $routes; do
                    stem="$evidence/t${trial}-n${count}-${order}-${phase}-${route}"
                    "$evidence/generate-chain" "$count" "$phase" "$route" "$order" > "$stem.metta"
                    sha256sum "$stem.metta" >> "$evidence/workloads.sha256"
                    code=0
                    /usr/bin/time -f '%e\t%M\t%x' -o "$stem.time.tsv" \
                        timeout "$deadline" env CETTA_PETTA_SEARCH_MACHINE=1 \
                        "$runtime" --lang petta "$program" lib/lib_bnf.metta "$stem.metta" \
                        > "$stem.stdout" 2> "$stem.stderr" || code=$?
                    status=failed
                    if [ "$code" -eq 124 ]; then
                        status=timeout
                    elif [ "$code" -eq 0 ] && [ ! -s "$stem.stderr" ] &&
                         [ "$(rg -F -x -c "(BnfBenchAccepted $count)" "$stem.stdout" || true)" = 1 ] &&
                         ! rg -q 'Error|BnfBench.*Refused|assertEqual|bench:' "$stem.stdout"; then
                        status=accepted
                    fi
                    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
                        "$trial" "$count" "$order" "$phase" "$route" "$status" \
                        "$(tail -n 1 "$stem.time.tsv")" | tee -a "$evidence/results.tsv"
                done
                pair=$((pair + 1))
            done
        done
    done
    trial=$((trial + 1))
done
sha256sum -c "$evidence/inputs.sha256" > "$evidence/inputs-after.txt"
awk -F '\t' 'NR > 1 && $6 != "accepted" { bad = 1 } END { exit bad }' "$evidence/results.tsv"
