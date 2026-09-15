#!/usr/bin/env bash
set -euo pipefail
if (( $# != 2 )); then
    echo 'usage: check_plain_bnf_selected_native_type_lean_v1.sh LEAN_ROOT EVIDENCE_DIRECTORY' >&2
    exit 2
fi
bnf_lean=$(realpath "$1")
bnf_root=$(cd -- "$(dirname -- "$0")/../.." && pwd -P)
mkdir -- "$2"
bnf_evidence=$(realpath "$2")
cd "$bnf_root"
bnf_client=tests/langdef/bnf/plain_bnf_provider_target_qualification_v1.lean
bnf_program=langdef/petta/generated/plain_bnf_semantic_admission_v1.metta
bnf_provider=experiments/gslt2parse_foundation/presentations/shared/cetta_petta_ground_integer_relations_v1.metta
bnf_admission=langdef/bnf/plain_bnf_semantic_admission_v1.metta
sha256sum "$bnf_client" "$bnf_program" "$bnf_provider" "$bnf_admission" "$0" \
    > "$bnf_evidence/inputs.sha256"
if rg -n '\b(sorry|admit|axiom|native_decide|theorem_wanted)\b|_wanted|set_option[[:space:]]+(maxHeartbeats|maxRecDepth|maxSteps)' \
        "$bnf_client" > "$bnf_evidence/source-audit.txt"; then
    echo 'selected NativeType client contains a proof placeholder or resource override' >&2
    exit 1
fi
(cd "$bnf_lean" && lake build Mettapedia.GSLT.Parsing.PlainBnfSelectedNativeTypeRefinement) \
    > "$bnf_evidence/build.log" 2>&1
(cd "$bnf_lean" && lake env lean -DwarningAsError=true "$bnf_root/$bnf_client") \
    > "$bnf_evidence/client.log" 2>&1
if rg -n 'sorryAx' "$bnf_evidence/client.log"; then exit 1; fi

# Both controls read evidence-local inputs against the already-built modules.
# No production artifact or source is replaced, and no parser failure counts
# as evidence that the selected correspondence detected a semantic mismatch.
awk '{ if (sub(/\(< \?left \?right\)/, "(>= ?left ?right)")) changed++; print }
     END { if (changed != 1) exit 1 }' "$bnf_provider" > "$bnf_evidence/provider.metta"
awk '{print} END {print "!(empty)"}' "$bnf_program" > "$bnf_evidence/extra-query.metta"
for bnf_case in provider extra-query; do
    bnf_selected_provider="$bnf_root/$bnf_provider"
    bnf_selected_program="$bnf_root/$bnf_program"
    bnf_theorem=initialization_command_exact
    if [[ "$bnf_case" == provider ]]; then
        bnf_selected_provider="$bnf_evidence/provider.metta"
        bnf_theorem=fresh_provider_snapshot
    else
        bnf_selected_program="$bnf_evidence/extra-query.metta"
    fi
    sed -e "s|../../../$bnf_program|$bnf_selected_program|g" \
        -e "s|../../../$bnf_provider|$bnf_selected_provider|g" \
        -e "s|../../../$bnf_admission|$bnf_root/$bnf_admission|g" \
        "$bnf_client" > "$bnf_evidence/$bnf_case.lean"
    if (cd "$bnf_lean" && lake env lean -DwarningAsError=true "$bnf_evidence/$bnf_case.lean") \
            > "$bnf_evidence/$bnf_case.log" 2>&1; then
        echo "source mismatch was accepted: $bnf_case" >&2; exit 1
    fi
    awk -v name="$bnf_theorem' depends on axioms:" '
        index($0, name) { selected = 1 }
        selected && /sorryAx/ { found = 1 }
        selected && /\]/ { selected = 0 }
        END { exit !found }
    ' "$bnf_evidence/$bnf_case.log"
    if rg -n 'parse failed|Unknown identifier|unknown constant|maximum number of heartbeats' \
            "$bnf_evidence/$bnf_case.log"; then
        echo 'control failed before the intended source comparison' >&2; exit 1
    fi
done
sha256sum -c "$bnf_evidence/inputs.sha256" > "$bnf_evidence/inputs-after.txt"
printf '(PlainBnfSelectedNativeTypeQualificationV1 3 0)\n'
