#!/usr/bin/env bash
set -euo pipefail
if (( $# < 3 || $# > 6 )); then
    echo 'usage: run_ebnf_workbench.sh RUNTIME ADMISSION EVIDENCE_DIRECTORY [BRANCH_COUNTS] [TRIALS] [SECONDS_PER_RUN]' >&2
    exit 2
fi
eb_runtime=$(realpath "$1")
eb_admission=$(realpath "$2")
mkdir -p "$3"
eb_evidence=$(realpath "$3")
eb_counts=${4:-'25 50 100 200 1000'}
eb_trials=${5:-3}
eb_deadline=${6:-60}
for eb_number in $eb_counts "$eb_trials" "$eb_deadline"; do
    [[ "$eb_number" =~ ^[1-9][0-9]*$ ]] || exit 2
done
test ! -e "$eb_evidence/results.tsv"
cd -- "$(dirname -- "$0")/../.."
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror \
    benchmarks/bnf/generate_ebnf_workbench.c -o "$eb_evidence/generate"
sha256sum "$eb_runtime" "$eb_admission" lib/lib_bnf.metta langdef/bnf/*.metta \
    langdef/bnf/*.bnf benchmarks/bnf/generate_ebnf_workbench.c "$0" > "$eb_evidence/inputs.sha256"
printf 'trial\tbranches\tsource_rules\tlowered_rules\thelpers\tphase\tstatus\telapsed_s\tpeak_rss_kib\texit_code\n' > "$eb_evidence/results.tsv"
for (( eb_trial=1; eb_trial<=eb_trials; ++eb_trial )); do
    eb_phases=(read lower declarations validate load parse project)
    for eb_count in $eb_counts; do
        for (( eb_i=0; eb_i<7; ++eb_i )); do
            eb_phase=${eb_phases[$(((eb_i + eb_trial - 1) % 7))]}
            eb_stem="$eb_evidence/t$eb_trial-n$eb_count-$eb_phase"
            "$eb_evidence/generate" "$eb_count" "$eb_phase" > "$eb_stem.metta"
            sha256sum "$eb_stem.metta" >> "$eb_evidence/workloads.sha256"
            eb_code=0
            /usr/bin/time -f '%e\t%M\t%x' -o "$eb_stem.time.tsv" \
                timeout "$eb_deadline" env CETTA_PETTA_SEARCH_MACHINE=1 \
                "$eb_runtime" --lang petta "$eb_admission" lib/lib_bnf.metta "$eb_stem.metta" \
                > "$eb_stem.out" 2> "$eb_stem.stderr" || eb_code=$?
            case "$eb_phase" in
                read) eb_expected="(EbnfBenchRead $((eb_count + 1)))";;
                lower) eb_expected="(EbnfBenchLower $((4 * eb_count + 1)) $((3 * eb_count)))";;
                declarations) eb_expected=EbnfBenchDeclarations;;
                validate) eb_expected=EbnfBenchValidated;;
                load) eb_expected=EbnfBenchLoaded;;
                parse) eb_expected='(EbnfBenchParsed 1)';;
                project) eb_expected=EbnfBenchProjected;;
            esac
            eb_status=failed
            if (( eb_code == 124 )); then eb_status=timeout
            elif (( eb_code == 0 )) && [[ ! -s "$eb_stem.stderr" ]] && \
                 [[ $(grep -Fxc "$eb_expected" "$eb_stem.out" || true) == 1 ]] && \
                 ! grep -Eq 'Error|Unexpected|Incomplete' "$eb_stem.out"; then eb_status=accepted
            fi
            printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$eb_trial" "$eb_count" \
                "$((eb_count + 1))" "$((4 * eb_count + 1))" "$((3 * eb_count))" \
                "$eb_phase" "$eb_status" "$(tail -n 1 "$eb_stem.time.tsv")" | tee -a "$eb_evidence/results.tsv"
        done
    done
done
sha256sum -c "$eb_evidence/inputs.sha256" > "$eb_evidence/inputs-after.txt"
awk -F '\t' 'NR > 1 && $7 != "accepted" { bad = 1 } END { exit bad }' "$eb_evidence/results.tsv"
