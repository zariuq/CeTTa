#!/usr/bin/env bash
set -euo pipefail
if (( $# < 3 || $# > 5 )); then
    echo 'usage: run_scalar_ablation.sh RUNTIME COMBINED_DISCOVERY_PROGRAM EVIDENCE_DIRECTORY [TRIALS] [SECONDS_PER_RUN]' >&2
    exit 2
fi
bnf_runtime=$(realpath "$1")
bnf_program=$(realpath "$2")
bnf_script=$(realpath "$0")
bnf_trials=${4:-3}
bnf_deadline=${5:-60}
for bnf_number in "$bnf_trials" "$bnf_deadline"; do
    if [[ ! $bnf_number =~ ^[0-9]+$ ]] ||
       ! test "$bnf_number" -gt 0 2>/dev/null; then
        echo "invalid positive integer: $bnf_number" >&2
        exit 2
    fi
done
bnf_trials=$((10#$bnf_trials))
bnf_deadline=$((10#$bnf_deadline))
test -x "$bnf_runtime" && test -f "$bnf_program"
mkdir -p -- "$(dirname -- "$3")"
if ! mkdir -- "$3"; then
    echo 'evidence directory must be new; existing files are never overwritten' >&2
    exit 2
fi
bnf_evidence=$(realpath "$3")
bnf_root=$(cd -- "$(dirname -- "$bnf_script")/../.." && pwd -P)
cd "$bnf_root"
bnf_helper=benchmarks/bnf/admitted_scalars.metta
bnf_head=admitted:gslt:fn:BNFScalarOrderDiagnosticV1:1110
rg -Fq '(gslt:admitted-entry:BNFValidateGrammarDiscoveryV1:10 ' "$bnf_program"
rg -Fq "($bnf_head " "$bnf_program"
sha256sum "$bnf_runtime" "$bnf_program" "$bnf_script" "$bnf_helper" \
    benchmarks/bnf/admitted_scalars_{raw,checked}.metta \
    lib/lib_bnf.metta lib/langdef.metta langdef/bnf/*.metta \
    > "$bnf_evidence/inputs.sha256"
git rev-parse HEAD > "$bnf_evidence/base-commit.txt"
git diff --cached --binary --no-ext-diff | sha256sum > "$bnf_evidence/staged-diff.sha256"
printf 'trial\troute\texit_code\tstats_records\tquery_transitions\n' > "$bnf_evidence/results.tsv"
bnf_failed=0

# Both routes load the same artifact and construct the same grammar. Only
# the actual raw/checked discovery entry differs. Every answer is printed.
for ((bnf_trial = 1; bnf_trial <= bnf_trials; bnf_trial++)); do
    bnf_routes=(raw checked)
    if (( bnf_trial % 2 == 0 )); then bnf_routes=(checked raw); fi
    for bnf_route in "${bnf_routes[@]}"; do
        bnf_stem="$bnf_evidence/t$bnf_trial-$bnf_route"
        bnf_code=0
        timeout "$bnf_deadline" env CETTA_PETTA_SEARCH_MACHINE=1 CETTA_PETTA_MACHINE_STATS=1 \
            "$bnf_runtime" --lang petta "$bnf_program" lib/lib_bnf.metta "$bnf_helper" \
            "benchmarks/bnf/admitted_scalars_$bnf_route.metta" \
            > "$bnf_stem.stdout" 2> "$bnf_stem.stderr" || bnf_code=$?
        # The workload file has one final top-level query. Its last stats
        # record includes input construction, admission, observation and close.
        bnf_stats=$(awk '/^PETTA_MACHINE_STATS / {
            for (i=1; i<=NF; i++) if ($i ~ /^transitions=[0-9]+$/) {
                split($i, value, "="); last=value[2]; count++
            }
        } END { printf "%d\t%s", count, count ? last : "missing" }' "$bnf_stem.stderr")
        printf '%s\t%s\t%s\t%s\n' "$bnf_trial" "$bnf_route" "$bnf_code" "$bnf_stats" \
            | tee -a "$bnf_evidence/results.tsv"
        if (( bnf_code != 0 )) ||
           [[ $bnf_stats == *missing ]] ||
           [[ $(rg -Fxc '(AdmittedScalarWorkAccepted 256)' "$bnf_stem.stdout" || true) != 1 ]] ||
           rg -q 'AdmittedScalarWorkUnexpected' "$bnf_stem.stdout" ||
           rg -qv '^PETTA_MACHINE_STATS ' "$bnf_stem.stderr"; then
            bnf_failed=1
        fi
    done
    if ! diff -u "$bnf_evidence/t$bnf_trial-raw.stdout" \
        "$bnf_evidence/t$bnf_trial-checked.stdout" > "$bnf_evidence/t$bnf_trial.diff"; then
        bnf_failed=1
    fi
done

# Work counters alone do not show that the type-guided family was reached.
# A separate trace run must leave the complete output unchanged; the raw
# call must not enter the admitted family.
for bnf_route in raw checked; do
    timeout "$bnf_deadline" env CETTA_PETTA_SEARCH_MACHINE=1 CETTA_PETTA_MACHINE_STATS=0 \
        CETTA_PETTA_QUERY_TRACE="$bnf_head" CETTA_PETTA_QUERY_TRACE_LIMIT=1 \
        "$bnf_runtime" --lang petta "$bnf_program" lib/lib_bnf.metta "$bnf_helper" \
        "benchmarks/bnf/admitted_scalars_$bnf_route.metta" \
        > "$bnf_evidence/$bnf_route.activation.stdout" \
        2> "$bnf_evidence/$bnf_route.activation.stderr"
    diff -u "$bnf_evidence/t1-$bnf_route.stdout" "$bnf_evidence/$bnf_route.activation.stdout" \
        > "$bnf_evidence/$bnf_route.activation.diff" || bnf_failed=1
done
test ! -s "$bnf_evidence/raw.activation.stderr" || bnf_failed=1
bnf_trace="^\\[petta-query\\] transitions=[0-9]+ head=$bnf_head bindings="
rg -q "$bnf_trace" "$bnf_evidence/checked.activation.stderr" || bnf_failed=1
if rg -qv "$bnf_trace" "$bnf_evidence/checked.activation.stderr"; then bnf_failed=1; fi
sha256sum -c "$bnf_evidence/inputs.sha256" > "$bnf_evidence/inputs-after.txt"
git diff --cached --binary --no-ext-diff | sha256sum > "$bnf_evidence/staged-diff-after.sha256"
cmp "$bnf_evidence/staged-diff.sha256" "$bnf_evidence/staged-diff-after.sha256"
printf 'ScalarDiscoveryAblation failures=%s\n' "$bnf_failed" > "$bnf_evidence/summary.txt"
exit "$bnf_failed"
