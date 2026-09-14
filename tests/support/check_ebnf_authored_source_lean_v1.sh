#!/usr/bin/env bash
set -euo pipefail
if (( $# != 2 )); then
    echo 'usage: check_ebnf_authored_source_lean_v1.sh LEAN_ROOT EVIDENCE_DIRECTORY' >&2
    exit 2
fi
ebnf_lean_root=$(realpath "$1")
ebnf_root=$(cd -- "$(dirname -- "$0")/../.." && pwd -P)
mkdir -- "$2"
ebnf_evidence=$(realpath "$2")
cd "$ebnf_root"
ebnf_client="$ebnf_root/tests/langdef/bnf/ebnf_authored_source_qualification_v1.lean"
ebnf_modules=(
    "$ebnf_lean_root/Mettapedia/GSLT/Parsing/EbnfWidthSource.lean"
    "$ebnf_lean_root/Mettapedia/GSLT/Parsing/EbnfHelperNameSource.lean"
    "$ebnf_lean_root/Mettapedia/GSLT/Parsing/EbnfRepetitionSource.lean")
sha256sum "$ebnf_client" "${ebnf_modules[@]}" \
    langdef/bnf/ebnf_lowering_v1.metta langdef/bnf/ebnf_derivation_projection_v1.metta \
    "$0" > "$ebnf_evidence/inputs.sha256"
if rg -n '\b(sorry|admit|axiom|native_decide|theorem_wanted)\b|_wanted|set_option[[:space:]]+(maxHeartbeats|maxRecDepth|maxSteps)' \
        "$ebnf_client" "${ebnf_modules[@]}" > "$ebnf_evidence/source-audit.txt"; then
    echo 'selected EBNF qualification contains a proof placeholder or resource override' >&2
    exit 1
fi
(cd "$ebnf_lean_root" && lake build Mettapedia.GSLT.Parsing.EbnfWidthSource) \
    > "$ebnf_evidence/build.log" 2>&1
(cd "$ebnf_lean_root" && lake env lean -DwarningAsError=true "$ebnf_client") \
    > "$ebnf_evidence/client.log" 2>&1
if rg -n 'sorryAx' "$ebnf_evidence/client.log"; then exit 1; fi

# A source mutation must be detected even with already-built theorem modules.
# Only evidence-local source copies change; no live authored file is replaced.
awk '
    /\(rule counter-one$/ {selected=1}
    selected && /\(bnf-v1:text-cons 48 \(ebnf-v1:next-bits \?tail\)\)/ {
        sub(/\(ebnf-v1:next-bits \?tail\)/, "?tail")
        changed++; selected=0
    }
    {print}
    END {if(changed != 1) exit 1}
' langdef/bnf/ebnf_lowering_v1.metta > "$ebnf_evidence/ebnf_lowering_v1.metta"
cp -- langdef/bnf/ebnf_derivation_projection_v1.metta "$ebnf_evidence/ebnf_derivation_projection_v1.metta"
sed 's|../../../langdef/bnf/||g' "$ebnf_client" > "$ebnf_evidence/mutated-client.lean"
set +e
(cd "$ebnf_lean_root" && lake env lean -DwarningAsError=true "$ebnf_evidence/mutated-client.lean") \
    > "$ebnf_evidence/mutation.log" 2>&1
ebnf_status=$?
set -e
test "$ebnf_status" -ne 0
grep -q 'freshLowering = EbnfRepetitionSource.loweringSyntax' "$ebnf_evidence/mutation.log"
sha256sum -c "$ebnf_evidence/inputs.sha256" > "$ebnf_evidence/inputs-after.txt"
printf '(EbnfAuthoredSourceQualificationV1 2 0)\n'
