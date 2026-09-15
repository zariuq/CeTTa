#!/usr/bin/env bash
set -euo pipefail
if (( $# != 3 )); then
    echo 'usage: check_plain_bnf_typed_lifetime_v1.sh RETAINED_CANDIDATE_DIRECTORY RUNTIME EVIDENCE_DIRECTORY' >&2
    exit 2
fi
bnf_script=$(realpath "$0")
bnf_repo=$(cd "$(dirname "$bnf_script")/../.." && pwd -P)
bnf_candidate=$(realpath "$1")
bnf_runtime=$(realpath "$2")
mkdir -p "$3"
bnf_evidence=$(realpath "$3")
test ! -e "$bnf_evidence/inputs.sha256"
cd "$bnf_repo"
bnf_settings=(BUILD=core ENABLE_GMP=0 ENABLE_LIB_PROLOG=0
    ENABLE_PRIME_NEED_HEAP_INDEX=0 ENABLE_PRIME_EVAL_STACK=0 JSON_BACKEND=gslt)
case $(basename "$bnf_runtime") in
    cetta-core.nogmp.json-gslt) ;;
    cetta-core.nogmp.sanitize.address-undefined.json-gslt)
        bnf_settings+=(ENABLE_SANITIZERS=1 "SANITIZERS=address,undefined") ;;
    *) echo 'unsupported retained runtime configuration' >&2; exit 2 ;;
esac
bnf_observer=tests/support/test_plain_bnf_typed_lifetime_v1.c
bnf_fixture=tests/langdef/bnf/plain_bnf_typed_lifetime_observation_v1.metta
bnf_public=tests/langdef/bnf/plain_bnf_typed_lifetime_public_v1.metta
bnf_ownership=tests/langdef/bnf/plain_bnf_typed_discovery_ownership_v1.metta
bnf_fuel_fixture=tests/langdef/bnf/plain_bnf_typed_lifetime_fuel_v1.metta
sha256sum "$bnf_runtime" "$bnf_script" "$bnf_observer" "$bnf_fixture" "$bnf_public" \
    "$bnf_fuel_fixture" "$bnf_ownership" "$bnf_candidate"/program.metta \
    "$bnf_candidate"/program-zero.metta "$bnf_candidate"/program-two.metta \
    "$bnf_candidate"/candidate-library.metta src/eval.h src/library.h \
    src/native_handle.h src/native_handle.c src/session.h \
    > "$bnf_evidence/inputs.sha256"

# Print only evaluated build variables. This target has no prerequisites and
# performs no regeneration or production compilation. Existing object files
# are inputs, pinned below; this is not a fresh-public-runtime build claim.
# The dollar expressions below belong to GNU Make, not this shell.
# shellcheck disable=SC2016
make --no-print-directory -s "${bnf_settings[@]}" \
    --eval 'bnf-lifetime-inputs: ; @printf "%s\n" "$(CC)" "$(CPPFLAGS) $(CFLAGS)" "$(LDFLAGS)" "$(OBJ)"' \
    bnf-lifetime-inputs > "$bnf_evidence/link-inputs.txt"
mapfile -t bnf_lines < "$bnf_evidence/link-inputs.txt"
test "${#bnf_lines[@]}" = 4
read -r -a bnf_cc <<< "${bnf_lines[0]}"
read -r -a bnf_flags <<< "${bnf_lines[1]}"
read -r -a bnf_ldflags <<< "${bnf_lines[2]}"
read -r -a bnf_all_objects <<< "${bnf_lines[3]}"
bnf_objects=()
for bnf_object in "${bnf_all_objects[@]}"; do
    if [[ ! -e $bnf_object && $bnf_object == src/gslt_language_manifest_v1.*.o ]]; then
        # The retained library object predates this decoder extraction too.
        # Refuse to omit it if that object actually requires the new API.
        bnf_old=${bnf_object/gslt_language_manifest_v1/library}
        nm -u "$bnf_old" > "$bnf_evidence/retained-library-symbols.txt"
        if rg -q 'cetta_gslt_language_manifest_parse_v1$' "$bnf_evidence/retained-library-symbols.txt"; then
            echo 'missing required extracted manifest decoder object' >&2; exit 1
        fi
        printf '%s\n' "$bnf_object" >> "$bnf_evidence/not-in-retained-runtime.txt"
        continue
    fi
    if [[ ! -e $bnf_object && $bnf_object == src/gslt_support_profile_v1.*.o ]]; then
        # The retained runtime predates this metadata-only extraction. Its
        # support-transform object still supplies the same validator symbol.
        bnf_old=${bnf_object/gslt_support_profile_v1/gslt_support_transform_runtime}
        nm -g "$bnf_old" > "$bnf_evidence/retained-support-symbols.txt"
        rg -q ' T cetta_gslt_support_transform_profile_validate_v1$' \
            "$bnf_evidence/retained-support-symbols.txt"
        printf '%s\n' "$bnf_object" >> "$bnf_evidence/not-in-retained-runtime.txt"
        continue
    fi
    if [[ ! -f $bnf_object ]]; then
        printf 'missing retained runtime object: %s\n' "$bnf_object" >&2
        exit 1
    fi
    bnf_objects+=("$bnf_object")
done
# Some retained NIK objects predate the differential-only extraction. Link
# their exact retained descriptors if required; never regenerate them or
# silently switch this unrelated consumer to a different implementation.
for bnf_object in "${bnf_objects[@]}"; do
    if [[ $bnf_object == src/nik_runtime.*.o ]]; then
        nm -u "$bnf_object" > "$bnf_evidence/retained-nik-symbols.txt"
        if rg -q ' U cetta_prime_nik_runtime_v1$' "$bnf_evidence/retained-nik-symbols.txt"; then
            bnf_tag=${bnf_object#src/nik_runtime.}
            for bnf_descriptor in prime_nik_runtime_v1 prime_nik_side_condition_provider_catalog_v1; do
                bnf_extra="src/generated/$bnf_descriptor.generated.$bnf_tag"
                test -f "$bnf_extra"
                bnf_objects+=("$bnf_extra")
                printf '%s\n' "$bnf_extra" >> "$bnf_evidence/retained-extra-objects.txt"
            done
        fi
        break
    fi
done
sha256sum "${bnf_objects[@]}" >> "$bnf_evidence/inputs.sha256"
bnf_link=("${bnf_cc[@]}" "${bnf_flags[@]}" "$bnf_observer" "${bnf_objects[@]}"
    '-Wl,--wrap=eval_top_with_registry_petta_plan'
    '-Wl,--wrap=cetta_library_dispatch_native'
    '-Wl,--wrap=cetta_library_context_free' "${bnf_ldflags[@]}"
    -o "$bnf_evidence/observe")
printf '%q ' "${bnf_link[@]}" > "$bnf_evidence/link-command.txt"
printf '\n' >> "$bnf_evidence/link-command.txt"
"${bnf_link[@]}" > "$bnf_evidence/compile.log" 2>&1
nm -u "$bnf_evidence/observe" > "$bnf_evidence/observer-symbols.txt"
if [[ $(basename "$bnf_runtime") == *sanitize* ]]; then
    rg -q '__asan_init$' "$bnf_evidence/observer-symbols.txt"
    rg -Fq -- '-fsanitize=address,undefined' "$bnf_evidence/link-inputs.txt"
else
    if rg -q '__asan_init$' "$bnf_evidence/observer-symbols.txt"; then
        echo 'unexpected sanitizer in normal observer' >&2; exit 1
    fi
fi
sha256sum "$bnf_evidence/observe" >> "$bnf_evidence/inputs.sha256"

for bnf_case in zero one two; do
    bnf_program="$bnf_candidate/program.metta"
    bnf_answers=1
    case $bnf_case in
        zero) bnf_program="$bnf_candidate/program-zero.metta"; bnf_answers=0 ;;
        two) bnf_program="$bnf_candidate/program-two.metta"; bnf_answers=2 ;;
    esac
    # Recheck the untouched retained executable, independent of the observer.
    CETTA_PETTA_SEARCH_MACHINE=1 "$bnf_runtime" --lang petta "$bnf_program" \
        "$bnf_candidate/candidate-library.metta" "$bnf_ownership" \
        > "$bnf_evidence/retained-$bnf_case.out" 2> "$bnf_evidence/retained-$bnf_case.err"
    test ! -s "$bnf_evidence/retained-$bnf_case.err"
    rg -Fxq "(PlainBnfTypedDiscoveryOwnershipV1 $bnf_answers OwnedAnswersAcceptedV1 OwnedHandleClosedV1)" \
        "$bnf_evidence/retained-$bnf_case.out"
    CETTA_PETTA_SEARCH_MACHINE=1 "$bnf_evidence/observe" --lang petta "$bnf_program" \
        "$bnf_candidate/candidate-library.metta" "$bnf_fixture" \
        > "$bnf_evidence/finite-$bnf_case.out" 2> "$bnf_evidence/finite-$bnf_case.err"
    test ! -s "$bnf_evidence/finite-$bnf_case.err"
    rg -Fq "complete answers=$bnf_answers live-before=1 live-after=0" "$bnf_evidence/finite-$bnf_case.out"
    rg -Fxq '(TypedLifetimeRecoveryCloseV1 False)' "$bnf_evidence/finite-$bnf_case.out"
    rg -Fxq '(TypedLifetimeFollowingQueryV1 42)' "$bnf_evidence/finite-$bnf_case.out"
done
# The retained executable itself takes the real incomplete-coverage path and
# exits normally through context teardown. No test observer mediates this run.
bnf_status=0
env CETTA_PETTA_SEARCH_MACHINE=1 \
    CETTA_PETTA_QUERY_TRACE=admitted:gslt:entry:BNFValidateGrammarDiscoveryV1:10 \
    "$bnf_runtime" --count-only --lang petta "$bnf_candidate/program.metta" \
    "$bnf_candidate/candidate-library.metta" "$bnf_fuel_fixture" \
    > "$bnf_evidence/retained-fuel.out" 2> "$bnf_evidence/retained-fuel.err" || bnf_status=$?
test "$bnf_status" = 1
printf '%s\n' "$bnf_status" > "$bnf_evidence/retained-fuel.status"
rg -Fxq 'error: count observation incomplete: fuel-exhausted' "$bnf_evidence/retained-fuel.err"
rg -q '^\[petta-query\] transitions=[0-9]+ head=admitted:gslt:entry:BNFValidateGrammarDiscoveryV1:10 bindings=' \
    "$bnf_evidence/retained-fuel.err"
test "$(wc -l < "$bnf_evidence/retained-fuel.err")" = 2
env CETTA_PETTA_SEARCH_MACHINE=1 BNF_LIFETIME_FUEL=4096 "$bnf_evidence/observe" --lang petta \
    "$bnf_candidate/program.metta" "$bnf_candidate/candidate-library.metta" "$bnf_fixture" \
    > "$bnf_evidence/finite-budgeted.out" 2> "$bnf_evidence/finite-budgeted.err"
test ! -s "$bnf_evidence/finite-budgeted.err"
rg -Fq 'complete answers=1 live-before=1 live-after=0' "$bnf_evidence/finite-budgeted.out"
rg -Fxq '(TypedLifetimeRecoveryCloseV1 False)' "$bnf_evidence/finite-budgeted.out"
for bnf_boundary in owned public; do
    bnf_selected="$bnf_fixture"
    bnf_before=1
    if [[ $bnf_boundary == public ]]; then bnf_selected="$bnf_public"; bnf_before=0; fi
    for bnf_stop in fuel abort; do
        bnf_env=(BNF_LIFETIME_FUEL=640
            CETTA_PETTA_QUERY_TRACE=admitted:gslt:entry:BNFValidateGrammarDiscoveryV1:10)
        if [[ $bnf_stop == abort ]]; then bnf_env=(BNF_LIFETIME_ABORT=1); fi
        bnf_name="$bnf_boundary-$bnf_stop"
        env CETTA_PETTA_SEARCH_MACHINE=1 "${bnf_env[@]}" "$bnf_evidence/observe" --lang petta \
            "$bnf_candidate/program.metta" "$bnf_candidate/candidate-library.metta" "$bnf_selected" \
            > "$bnf_evidence/$bnf_name.out" 2> "$bnf_evidence/$bnf_name.err"
        # An escaped caller-owned handle remains live; the public operation's
        # unreturned temporary handle must be reclaimed before context teardown.
        rg -Fq "answers=0 live-before=$bnf_before live-after=$bnf_before" "$bnf_evidence/$bnf_name.out"
        if [[ $bnf_stop == fuel ]]; then
            # Actual typed admission begins before the metered machine stops.
            # This excludes an exhaustion confined to loading or orchestration.
            test "$(wc -l < "$bnf_evidence/$bnf_name.err")" = 1
            rg -q '^\[petta-query\] transitions=[0-9]+ head=admitted:gslt:entry:BNFValidateGrammarDiscoveryV1:10 bindings=' \
                "$bnf_evidence/$bnf_name.err"
            rg -Fq '(BnfLifetimeObservationV1 fuel-exhausted ' "$bnf_evidence/$bnf_name.out"
            rg -Fq 'steps=640 abort-requested=0 exit-observed=0)' "$bnf_evidence/$bnf_name.out"
        else
            test ! -s "$bnf_evidence/$bnf_name.err"
            rg -Fq 'abort-requested=1 exit-observed=1)' "$bnf_evidence/$bnf_name.out"
        fi
        rg -Fxq '(TypedLifetimeFollowingQueryV1 42)' "$bnf_evidence/$bnf_name.out"
        bnf_final=0
        if [[ $bnf_boundary == owned ]]; then
            bnf_final=0
            rg -Fxq '(TypedLifetimeRecoveryCloseV1 True)' "$bnf_evidence/$bnf_name.out"
        fi
        rg -Fxq "(BnfLifetimeContextTeardownV1 live-before=$bnf_final live-after=0 probes=1)" \
            "$bnf_evidence/$bnf_name.out"
    done
done
sha256sum -c "$bnf_evidence/inputs.sha256" > "$bnf_evidence/inputs-after.txt"
printf '(PlainBnfTypedLifetimeV1Summary finite=4 stopped=4 expected-retention=4)\n' | tee "$bnf_evidence/summary.txt"
