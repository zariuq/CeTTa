#!/bin/sh
set -eu

if [ "$#" -lt 3 ] || [ "$#" -gt 6 ]; then
    echo 'usage: run_admission.sh RUNTIME GENERATED_ADMISSION EVIDENCE_DIRECTORY [RULE_COUNTS] [TRIALS] [SECONDS_PER_RUN]' >&2
    exit 2
fi
runtime=$(realpath "$1")
admission=$(realpath "$2")
evidence=$3
counts=${4:-'25 50 100 200 1000'}
trials=${5:-3}
deadline=${6:-60}
root=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)

for number in $counts "$trials" "$deadline"; do
    case $number in ''|*[!0-9]*|0) echo "invalid positive integer: $number" >&2; exit 2;; esac
done
test -x "$runtime" && test -f "$admission"
mkdir -p "$evidence"
evidence=$(realpath "$evidence")
# An evidence directory is a single immutable run, never an append target.
if [ -e "$evidence/results.tsv" ]; then
    echo 'results.tsv already exists; use a new evidence directory' >&2
    exit 2
fi
cd "$root"
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror \
    benchmarks/bnf/generate_chain.c -o "$evidence/generate-chain"
sha256sum "$runtime" "$admission" lib/lib_bnf.metta \
    langdef/bnf/*.metta benchmarks/bnf/generate_chain.c \
    benchmarks/bnf/run_admission.sh > "$evidence/inputs.sha256"
git rev-parse HEAD > "$evidence/base-commit.txt"
git diff --cached | sha256sum > "$evidence/staged-diff.sha256"
git diff | sha256sum > "$evidence/unstaged-diff.sha256"
uname -srm > "$evidence/platform.txt"
${CC:-cc} --version > "$evidence/compiler.txt"
printf 'trial\trules\tphase\tstatus\telapsed_s\tpeak_rss_kib\texit_code\n' > "$evidence/results.tsv"

trial=1
while [ "$trial" -le "$trials" ]; do
    # Rotate phase order so later stages are not always last in a trial.
    case $((trial % 3)) in
        1) phases='read declarations validate';;
        2) phases='validate read declarations';;
        0) phases='declarations validate read';;
    esac
    for count in $counts; do
        for phase in $phases; do
            stem="$evidence/t${trial}-n${count}-${phase}"
            "$evidence/generate-chain" "$count" "$phase" > "$stem.metta"
            sha256sum "$stem.metta" >> "$evidence/workloads.sha256"
            code=0
            /usr/bin/time -f '%e\t%M\t%x' -o "$stem.time.tsv" \
                timeout "$deadline" env CETTA_PETTA_SEARCH_MACHINE=1 \
                "$runtime" --lang petta "$admission" lib/lib_bnf.metta \
                "$stem.metta" > "$stem.stdout" 2> "$stem.stderr" || code=$?
            status=failed
            if [ "$code" -eq 124 ]; then
                status=timeout
            elif [ "$code" -eq 0 ] && [ ! -s "$stem.stderr" ] && \
                 [ "$(rg -F -x -c "(BnfBenchAccepted $count)" "$stem.stdout" || true)" = 1 ] && \
                 ! rg -q 'Error|assertEqual|bench:observe|bench:run' "$stem.stdout"; then
                status=accepted
            fi
            metrics=$(tail -n 1 "$stem.time.tsv")
            printf '%s\t%s\t%s\t%s\t%s\n' \
                "$trial" "$count" "$phase" "$status" "$metrics" \
                | tee -a "$evidence/results.tsv"
        done
    done
    trial=$((trial + 1))
done
sha256sum -c "$evidence/inputs.sha256" > "$evidence/inputs-after.txt"
# Exit success only when every run completed with the expected public result.
awk -F '\t' 'NR > 1 && $4 != "accepted" { bad = 1 } END { exit bad }' \
    "$evidence/results.tsv"
