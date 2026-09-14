#!/bin/sh
set -eu
if [ "$#" -lt 5 ] || [ "$#" -gt 8 ]; then
    echo 'usage: run_public_comparison.sh RUNTIME PROGRAM RAW_LIBRARY CHECKED_LIBRARY EVIDENCE_DIRECTORY [RULE_COUNTS] [TRIALS] [SECONDS_PER_RUN]' >&2
    exit 2
fi
runtime=$(realpath "$1")
program=$(realpath "$2")
raw_library=$(realpath "$3")
checked_library=$(realpath "$4")
mkdir -p "$5"
evidence=$(realpath "$5")
counts=${6:-'25 50 100 200 1000'}
trials=${7:-3}
deadline=${8:-60}
for number in $counts "$trials" "$deadline"; do
    case $number in ''|*[!0-9]*) echo "invalid positive integer: $number" >&2; exit 2;; esac
    if ! [ "$number" -gt 0 ] 2>/dev/null; then
        echo "invalid positive integer: $number" >&2
        exit 2
    fi
done
test -x "$runtime" && test -f "$program"
test -f "$raw_library" && test -f "$checked_library"
test ! -e "$evidence/results.tsv"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror \
    benchmarks/bnf/generate_chain.c -o "$evidence/generate-chain"
sha256sum "$runtime" "$program" "$raw_library" "$checked_library" \
    lib/langdef.metta langdef/bnf/*.metta benchmarks/bnf/generate_chain.c \
    benchmarks/bnf/run_public_comparison.sh > "$evidence/inputs.sha256"
git rev-parse HEAD > "$evidence/base-commit.txt"
git diff --cached | sha256sum > "$evidence/staged-diff.sha256"
printf 'trial\trules\torder\troute\tstatus\telapsed_s\tuser_s\tsystem_s\tpeak_rss_kib\texit_code\n' > "$evidence/results.tsv"

# One source-text workload per pair; both candidates load the identical
# generated program and use bnf:validate. Only the supplied library differs.
for count in $counts; do
    for order in forward reverse; do
        workload="$evidence/n$count-$order.metta"
        "$evidence/generate-chain" "$count" validate linear "$order" linear > "$workload"
        sha256sum "$workload" >> "$evidence/workloads.sha256"
    done
done
trial=1
failed=0
while [ "$trial" -le "$trials" ]; do
    pair=0
    for count in $counts; do
        for order in forward reverse; do
            case $(((trial + pair) % 2)) in
                0) routes='checked raw';;
                1) routes='raw checked';;
            esac
            for route in $routes; do
                library=$raw_library
                if [ "$route" = checked ]; then library=$checked_library; fi
                stem="$evidence/t$trial-n$count-$order-$route"
                code=0
                /usr/bin/time -f '%e\t%U\t%S\t%M\t%x' -o "$stem.time.tsv" \
                    timeout "$deadline" env CETTA_PETTA_SEARCH_MACHINE=1 CETTA_PETTA_MACHINE_STATS=1 \
                    "$runtime" --lang petta "$program" "$library" "$evidence/n$count-$order.metta" \
                    > "$stem.stdout" 2> "$stem.stderr" || code=$?
                status=failed
                if [ "$code" -eq 124 ]; then
                    status=timeout
                elif [ "$code" -eq 0 ] &&
                     ! rg -v '^PETTA_MACHINE_STATS ' "$stem.stderr" | rg -q . &&
                     [ "$(rg -F -x -c "(BnfBenchAccepted $count)" "$stem.stdout" || true)" = 1 ] &&
                     ! rg -v "^(true|\\(BnfBenchAccepted $count\\))$" "$stem.stdout" | rg -q .; then
                    status=accepted
                fi
                if [ "$status" != accepted ]; then failed=1; fi
                # GNU time's %x may be zero after signal termination. Retain
                # that raw record, but publish the captured shell status.
                printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$trial" "$count" "$order" "$route" "$status" \
                    "$(awk -F '\t' 'END { printf "%s\t%s\t%s\t%s", $1, $2, $3, $4 }' "$stem.time.tsv")" \
                    "$code" | tee -a "$evidence/results.tsv"
            done
            if ! cmp "$evidence/t$trial-n$count-$order-raw.stdout" \
                     "$evidence/t$trial-n$count-$order-checked.stdout" \
                     > "$evidence/t$trial-n$count-$order.comparison" 2>&1; then failed=1; fi
            pair=$((pair + 1))
        done
    done
    trial=$((trial + 1))
done
sha256sum -c "$evidence/inputs.sha256" > "$evidence/inputs-after.txt"
sha256sum -c "$evidence/workloads.sha256" > "$evidence/workloads-after.txt"
exit "$failed"
