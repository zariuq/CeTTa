#!/usr/bin/env bash
set -euo pipefail
if (( $# < 2 || $# > 3 )); then
    echo 'usage: check_rule_machine_program_native_v1.sh TOOL EVIDENCE_DIRECTORY [REFERENCE_HEADER]' >&2
    exit 2
fi
rule_tool=$(realpath "$1")
rule_script=$(realpath "$0")
rule_repo=$(cd "$(dirname "$rule_script")/../.." && pwd -P)
mkdir -p "$2"
rule_evidence=$(realpath "$2")
test ! -e "$rule_evidence/inputs.sha256"
cd "$rule_repo"
rule_reference=$(realpath "${3:-src/generated/rule_machine_program_v1.generated.h}")
rule_core=experiments/gslt2parse_foundation/presentations/core/rule_machine_core_v1.metta
rule_program=experiments/gslt2parse_foundation/presentations/specializations/rule_machine_hilbert_bfc_program_v1.metta
rule_observer=tests/support/test_rule_machine_program_generated_v1.c
rule_sources=(src/atom.c src/binding/frame_identity.c src/symbol.c src/name_key.c src/match.c src/binding/frame_schema.c src/binding/slot_store.c src/binding/activation_view.c
    src/prime_need.c src/variant_shape.c src/term_universe.c src/term_canon.c)
sha256sum "$rule_tool" "$rule_script" "$rule_reference" "$rule_core" "$rule_program" \
    "$rule_observer" tools/rule_machine_program_v1.c tools/gslt_native_emission_v1.h \
    native/operational_language_def_v1.c native/gslt_composition_v1.c \
    src/rule_machine.c src/rule_machine.h src/generated/cetta_execution_contracts.generated.h \
    "${rule_sources[@]}" > "$rule_evidence/inputs.sha256"
rule_cc=${CC:-cc}
read -r -a rule_flags <<< "${CFLAGS:--O2 -Wall -Werror}"
rule_flags+=(-std=c11 -g -pthread -ffunction-sections -fdata-sections -Isrc -I.
    -DCETTA_BUILD_WITH_GMP=0 -DCETTA_BUILD_WITH_RUNTIME_STATS=0)
rule_objects=()
for rule_source in "${rule_sources[@]}"; do
    rule_object="$rule_evidence/$(basename "${rule_source%.c}").o"
    "$rule_cc" "${rule_flags[@]}" -c "$rule_source" -o "$rule_object" \
        >> "$rule_evidence/compile.log" 2>&1
    rule_objects+=("$rule_object")
done

for rule_case in original alpha hyp-one strings; do
    rule_dir="$rule_evidence/$rule_case"
    mkdir -p "$rule_dir/first/generated" "$rule_dir/second/generated"
    if [[ $rule_case == alpha ]]; then
        sed -E 's/\?([[:alnum:]_-]+)/?renamed_\1/g' "$rule_core" > "$rule_dir/core.metta"
        sed -E 's/\?([[:alnum:]_-]+)/?renamed_\1/g' "$rule_program" > "$rule_dir/program.metta"
    else
        if [[ $rule_case == strings ]]; then
            sed '/(rewrites/a\    (rule quoted-source (head (source-block ?guest "quoted\\\" slash\\\\ tab\\t lambda-λ")) (body))\n    (rule empty-source (head (source-block ?guest "")) (body))' \
                "$rule_core" > "$rule_dir/core.metta"
        else cp "$rule_core" "$rule_dir/core.metta"; fi
        if [[ $rule_case == hyp-one ]]; then
            sed 's/(rmbc-hyp-add 2)/(rmbc-hyp-add 1)/' "$rule_program" > "$rule_dir/program.metta"
        else cp "$rule_program" "$rule_dir/program.metta"; fi
    fi
    for rule_trial in first second; do
        "$rule_tool" --core "$rule_dir/core.metta" --program-gslt "$rule_dir/program.metta" \
            --out "$rule_dir/$rule_trial/generated/rule_machine_program_v1.generated.h" \
            > "$rule_dir/$rule_trial/generate.stdout" 2> "$rule_dir/$rule_trial/generate.stderr"
        test ! -s "$rule_dir/$rule_trial/generate.stderr"
    done
    cmp "$rule_dir/first/generated/rule_machine_program_v1.generated.h" \
        "$rule_dir/second/generated/rule_machine_program_v1.generated.h"
    # The copied production C file resolves its quoted generated include locally.
    # Its bytes and all shared production files remain unchanged.
    cp src/rule_machine.c "$rule_dir/first/rule_machine.c"
    cmp src/rule_machine.c "$rule_dir/first/rule_machine.c"
    rule_command=("$rule_cc" "${rule_flags[@]}" "$rule_observer" "${rule_objects[@]}"
        "-DRULE_PROGRAM_TEST_SOURCE=\"$rule_dir/first/rule_machine.c\""
        "-DRULE_PROGRAM_REFERENCE_HEADER=\"$rule_reference\""
        '-Wl,--gc-sections' -lm -o "$rule_dir/observe")
    printf '%q ' "${rule_command[@]}" > "$rule_dir/compile.command"
    printf '\n' >> "$rule_dir/compile.command"
    "${rule_command[@]}" >> "$rule_evidence/compile.log" 2>&1
    rule_core_sha=$(sha256sum "$rule_dir/core.metta"); rule_core_sha=${rule_core_sha%% *}
    rule_program_sha=$(sha256sum "$rule_dir/program.metta"); rule_program_sha=${rule_program_sha%% *}
    rule_hypotheses=2
    if [[ $rule_case == hyp-one ]]; then rule_hypotheses=1; fi
    "$rule_dir/observe" "$rule_hypotheses" "$rule_core_sha" "$rule_program_sha" \
        > "$rule_dir/observe.log" 2>&1
    sed -n '1,5p' "$rule_dir/observe.log"
done

rule_controls="$rule_evidence/controls"
mkdir -p "$rule_controls"
cp "$rule_core" "$rule_controls/core.metta"
cp "$rule_program" "$rule_controls/program.metta"
cp "$rule_evidence/original/first/generated/rule_machine_program_v1.generated.h" "$rule_controls/retained.h"
rule_refusals=0
rule_refuse() {
    local label=$1
    shift
    cp "$rule_controls/retained.h" "$rule_controls/output.h"
    if "$rule_tool" "$@" > "$rule_controls/$label.stdout" 2> "$rule_controls/$label.stderr"; then
        echo "unexpected native rule-program acceptance: $label" >&2; exit 1
    fi
    rg -q 'RuleMachineProgramGenerationError:|error:|usage:' "$rule_controls/$label.stderr"
    cmp "$rule_controls/output.h" "$rule_controls/retained.h"
    cmp "$rule_controls/core.metta" "$rule_core"
    cmp "$rule_controls/program.metta" "$rule_program"
    rule_refusals=$((rule_refusals + 1))
}
rule_mutation() {
    local label=$1 source=$2 script=$3
    sed "$script" "$source" > "$rule_controls/$label.metta"
    if cmp -s "$source" "$rule_controls/$label.metta"; then
        echo "ineffective source mutation: $label" >&2; exit 1
    fi
    local core="$rule_controls/core.metta" program="$rule_controls/program.metta"
    if [[ $source == "$rule_core" ]]; then core="$rule_controls/$label.metta"
    else program="$rule_controls/$label.metta"; fi
    rule_refuse "$label" --core "$core" --program-gslt "$program" --out "$rule_controls/output.h"
}

rule_mutation input-head "$rule_program" '0,/(bc-match ?schema)/s//(bc-match changed)/'
rule_mutation input-premises "$rule_program" '0,/(bc-goals rm-nil)/s//(bc-goals ?schema)/'
rule_mutation id-sharing "$rule_program" '0,/?id ?source (rule-program-template/s//?source ?id (rule-program-template/'
rule_mutation schema-sharing "$rule_program" '0,/(rule-program-template ?schema)/s//(rule-program-template ?source)/'
rule_mutation binder-collapse "$rule_program" 's/?source/?id/g'
rule_mutation premise-sharing "$rule_program" 's/(rm-premise ?right (imp ?antecedent ?conclusion))/(rm-premise ?right (imp ?conclusion ?antecedent))/'
rule_mutation proof-sharing "$rule_program" 's/(rm-cons ?left (rm-cons ?right rm-nil))/(rm-cons ?right (rm-cons ?left rm-nil))/'
rule_mutation proof-operand "$rule_program" 's/(rmbc-proof-wrap ?proof)/(rmbc-proof-wrap ?right)/'
rule_mutation output-template "$rule_program" 's/(rule-program-template rule-program-none)/(rule-program-template ?conclusion)/'
rule_mutation body-added "$rule_program" '0,/(body)/s//(body (compile-block ?id ?source))/'
rule_mutation bridge-order "$rule_program" 's/(source-block-id ?guest ?id ?source)/SWAP/;s/(compile-block ?source ?bytecode)/(source-block-id ?guest ?id ?source)/;s/SWAP/(compile-block ?source ?bytecode)/'
rule_mutation core-transport "$rule_core" '0,/(bc-match ?conclusion)/s//(bc-match ?source)/'
rule_mutation rename-clause "$rule_program" 's/compile-rule-program-inverse-mp/other-clause/'
rule_mutation renamed-relation "$rule_program" 's/compile-rule-program-block/other-compiler/g'
rule_mutation unknown-op "$rule_program" 's/rmbc-fresh/rmbc-unknown/g'
rule_mutation register "$rule_program" 's/(rmbc-fresh r2)/(rmbc-fresh r6)/'
rule_mutation integer-overflow "$rule_program" 's/(rmbc-hyp-add 2)/(rmbc-hyp-add 2147483648)/'
rule_mutation noncanonical-int "$rule_program" 's/(rmbc-hyp-add 2)/(rmbc-hyp-add 02)/'
rule_mutation negative-int "$rule_program" 's/(rmbc-hyp-add 2)/(rmbc-hyp-add -1)/'
rule_mutation malformed-tail "$rule_program" 's/rule-program-nil/bad-tail/g'
rule_mutation extra-clause "$rule_program" '/(rewrites/a\    (rule extra-compiler (head (compile-rule-program-block ?input ?output)) (body))'
rule_mutation extra-core-clause "$rule_core" '/(rewrites/a\    (rule extra-core (head (compile-block ?input ?output)) (body))'
rule_mutation duplicate-clause "$rule_program" '/(rewrites/a\    (rule compile-rule-program-axiom (head (compile-rule-program-block ?input ?output)) (body))'
rule_mutation undeclared-unselected "$rule_core" 's/(source-block ?guest ?source)/(undeclared ?guest ?source)/'
rule_mutation duplicate-operator "$rule_program" '/(signature/a\    (operator imp 2)'
rule_mutation nonempty-equations "$rule_program" 's/(equations)/(equations (equation (imp ?x ?y) (imp ?y ?x)))/'
rule_mutation source-identity "$rule_program" 's/HilbertBFCProgramSpecializationV1/OtherProgramV1/'
rule_mutation extra-top-level "$rule_program" '1i\ (extra-source)'
rule_mutation embedded-nul "$rule_core" '/(rewrites/a\    (rule nul-source (head (source-block ?guest "nul\\x00tail")) (body))'

rule_args=(--core "$rule_controls/core.metta" --program-gslt "$rule_controls/program.metta" --out "$rule_controls/output.h")
rule_refuse no-args
rule_refuse unpaired --core
rule_refuse missing --core "$rule_controls/core.metta" --out "$rule_controls/output.h"
rule_refuse repeated "${rule_args[@]}" --core "$rule_controls/core.metta"
rule_refuse unknown "${rule_args[@]}" --extra foo
rule_refuse empty --core '' --program-gslt "$rule_controls/program.metta" --out "$rule_controls/output.h"
ln "$rule_controls/core.metta" "$rule_controls/core-hardlink"
ln -s program.metta "$rule_controls/program-symlink"
ln -s absent "$rule_controls/dangling"
for rule_alias in core.metta core-hardlink program.metta program-symlink dangling; do
    rule_refuse "alias-$rule_alias" --core "$rule_controls/core.metta" \
        --program-gslt "$rule_controls/program.metta" --out "$rule_controls/$rule_alias"
done
rule_refuse alias-executable --core "$rule_controls/core.metta" \
    --program-gslt "$rule_controls/program.metta" --out "$rule_tool"
test -L "$rule_controls/dangling"
test ! -e "$rule_controls/absent"
sha256sum -c "$rule_evidence/inputs.sha256" > "$rule_evidence/inputs-unchanged.log"
printf 'Native rule-program generation: 4 deterministic source cases, production loader/dispatch agreement, %s explicit refusals.\n' "$rule_refusals"
