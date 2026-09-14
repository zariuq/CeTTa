#!/usr/bin/env bash
set -euo pipefail
if (( $# < 3 || $# > 6 )); then
    echo 'usage: run_ebnf_stages.sh RUNTIME ADMISSION EVIDENCE_DIRECTORY [BRANCH_COUNTS] [TRIALS] [SECONDS_PER_RUN]' >&2
    exit 2
fi
runtime=$(realpath "$1")
admission=$(realpath "$2")
mkdir -- "$3"
evidence=$(realpath "$3")
counts=${4:-'25 50 100 200 1000'}
trials=${5:-3}
deadline=${6:-600}
for number in $counts "$trials" "$deadline"; do
    [[ "$number" =~ ^[1-9][0-9]*$ ]] || exit 2
done
test ! -e "$evidence/results.tsv"
cd -- "$(dirname -- "$0")/../.."
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror \
    benchmarks/bnf/generate_ebnf_workbench.c -o "$evidence/generate"
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror \
    benchmarks/bnf/observe_ebnf_stages.c -o "$evidence/observe"
sha256sum "$runtime" "$admission" lib/lib_bnf.metta langdef/bnf/*.metta \
    benchmarks/bnf/{generate_ebnf_workbench.c,observe_ebnf_stages.c,run_ebnf_stages.sh} \
    > "$evidence/inputs.sha256"
printf 'trial\tbranches\tsource_rules\tlowered_rules\thelpers\tstatus\texit_code\tobserver_exit\telapsed_s\tpeak_rss_kib\ttime_exit\n' > "$evidence/results.tsv"
bad=0
for (( trial=1; trial<=trials; ++trial )); do
    for count in $counts; do
        stem="$evidence/t$trial-n$count"
        "$evidence/generate" "$count" stages > "$stem.metta"
        sha256sum "$stem.metta" >> "$evidence/workloads.sha256"
        set +e
        /usr/bin/time -f '%e\t%M\t%x' -o "$stem.time.tsv" \
            timeout "$deadline" env CETTA_PETTA_SEARCH_MACHINE=1 \
            "$runtime" --lang petta "$admission" lib/lib_bnf.metta "$stem.metta" \
            2> "$stem.stderr" | "$evidence/observe" "$stem.stages.tsv" > "$stem.out"
        codes=("${PIPESTATUS[@]}")
        set -e
        status=failed
        if (( codes[0] == 124 )); then
            status=timeout
        elif (( codes[0] == 0 && codes[1] == 0 )) && [[ ! -s "$stem.stderr" ]] &&
             [[ $(grep -Fxc "(EbnfBenchStages $((4 * count + 1)) $((3 * count)))" "$stem.out" || true) == 1 ]]; then
            status=accepted
        fi
        [[ "$status" == accepted ]] || bad=1
        printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
            "$trial" "$count" "$((count + 1))" "$((4 * count + 1))" "$((3 * count))" \
            "$status" "${codes[0]}" "${codes[1]}" "$(tail -n 1 "$stem.time.tsv")" \
            | tee -a "$evidence/results.tsv"
    done
done
sha256sum -c "$evidence/inputs.sha256" > "$evidence/inputs-after.txt"
exit "$bad"
