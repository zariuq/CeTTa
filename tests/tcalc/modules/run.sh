#!/bin/sh
# Rule modules read as tcalc programs by demand (lib/tcalc_modules.metta, docs/tcalc.md section 10).
#   sh tests/tcalc/modules/run.sh CETTA WORK        (from the CeTTa root)
# - NAME.module with NAME.rows: the module read by demand, with the rows (a relation's rows and the
#   calls asked), runs as NAME.expected says;
# - refused_NAME.module: the reading refuses it as refused_NAME.expected says;
# - choice.module: rulec refuses the overlap whose call the reading answers with two values;
# - the lambda client of rulec (tests/rulec/lambda): rulec's compiled C and the reading agree on
#   every case;
# - the Megalodon kernel (megalodon_kernel.tcalc, the reading of the module mgmodule is compiled
#   from): the calls of tests/rulec/mgmodule/handles.metta, captured from mgmodule as it runs, and
#   the proof checks of megalodon_proofs.cap agree with the compiled module's results.
# The first line of a module, "-- theory: NAME", names the theory to read (Lambda's is Calc).
set -eu
bin=$1
work=$2
here=tests/tcalc/modules
root=$(pwd)
mkdir -p "$work"   # every file below is written afresh on each run
fail=0
cc=${CC:-cc}

translate() {   # translate MODULE OUT [THEORY]: prints the reading's answer
    theory=${3:-$(sed -n '1s/^-- theory: //p' "$1")}
    printf '!(import! &self "%s/lib/tcalc_modules.metta")\n!(tcalc:module-write "%s/%s" "%s" "%s")\n' \
        "$root" "$root" "$1" "$theory" "$2" > "$work/translate.metta"
    "$bin" "$work/translate.metta" 2>&1 | tail -1
}

# the readings with their rows, and the refusals
for m in "$here"/*.module; do
    n=$(basename "$m" .module)
    case $n in
        refused_*)
            got=$(translate "$m" "$work/$n.tcalc")
            if [ "$got" != "$(cat "$here/$n.expected")" ]; then
                echo "FAIL: $n: $got"; fail=1
            fi
            continue ;;
    esac
    [ -f "$here/$n.rows" ] || continue
    got=$(translate "$m" "$work/$n.tcalc")
    case $got in
        "(tcalc:module-written "*) ;;
        *) echo "FAIL: $n is not read: $got"; fail=1; continue ;;
    esac
    cat "$here/$n.rows" >> "$work/$n.tcalc"
    "$bin" --lang tcalc --profile observe=outcomes "$work/$n.tcalc" > "$work/$n.out" 2>&1 || true
    if ! cmp -s "$work/$n.out" "$here/$n.expected"; then
        echo "FAIL: $n runs otherwise"; diff "$here/$n.expected" "$work/$n.out" | head -10; fail=1
    fi
done

# rulec, its runtime and components, as tests/rulec/lambda/gate.sh builds them
$cc -O2 -o "$work/rulec" "$root"/tools/rulec/compiler/*.c
wire() {   # wire MODULE THEORY OUT: the module's core rows, printed by CeTTa's own module reader
    printf '!(import! &self "%s/tools/rulec/dump_wire.metta")\n!(wire:dump "%s" "%s")\n' \
        "$root" "$root/$1" "$2" > "$work/dump.metta"
    "$bin" "$work/dump.metta" | grep '^(' > "$3"
}

# choice: rulec refuses what the reading answers with two values
wire "$here/choice.module" Calc "$work/choice.wire"
if "$work/rulec" "$work/choice.wire" --emit "$work/choice.c" --emit-h "$work/choice.h" > "$work/choice.rulec" 2>&1; then
    echo "FAIL: rulec compiles choice.module"; fail=1
elif ! grep -q 'left side overlaps rule' "$work/choice.rulec"; then
    echo "FAIL: rulec refuses choice.module for another reason:"; cat "$work/choice.rulec"; fail=1
fi

# lambda: the compiled module (every rule as written) against the reading, case by case
L=tests/rulec/lambda
wire "$L/Lambda.module" Calc "$work/Lambda.wire"
"$work/rulec" "$work/Lambda.wire" --cache-constants --emit "$work/lambda.c" --emit-h "$work/lambda_ids.h" 2> "$work/lambda.rulec"
$cc -O2 -std=gnu11 -Inative/rule_runtime -Inative/rule_components -I"$work" \
    "$work/lambda.c" "$here/lam_tcalc.c" native/rule_runtime/rc_rt.c native/rule_components/*.c -o "$work/lam_tcalc"
got=$(translate "$L/Lambda.module" "$work/lambda.tcalc" Calc)
case $got in "(tcalc:module-written "*) ;; *) echo "FAIL: Lambda.module is not read: $got"; fail=1 ;; esac
mkdir -p "$work/lambda"
if out=$("$work/lam_tcalc" "$bin" "$work/lambda.tcalc" "$L/cases.txt" "$work/lambda"); then
    printf '%s\n' "$out" | tail -1
else
    printf '%s\n' "$out"; fail=1
fi

# Megalodon: calls captured from mgmodule now, and the recorded proof checks
$cc -O2 -std=gnu11 -o "$work/cap_tcalc" "$here/cap_tcalc.c"
mkdir -p "$work/megalodon"
capture=$(mktemp -d "$work/capture.XXXXXX")
RCM_CAPTURE="$capture" "$bin" --lang he tests/rulec/mgmodule/handles.metta > "$work/handles.out" 2>&1
set -- "$capture"/mgm.*.cap
[ -f "$1" ] || { echo "FAIL: mgmodule wrote no capture"; fail=1; }
for c in "handles:$1" "proofs:$here/megalodon_proofs.cap"; do
    if out=$("$work/cap_tcalc" "$bin" "$here/megalodon_kernel.tcalc" "$work/megalodon" "megalodon ${c%%:*}" "${c#*:}"); then
        printf '%s\n' "$out" | tail -1
    else
        printf '%s\n' "$out"; fail=1
    fi
done
rm -f "$capture"/mgm.*.cap
rmdir "$capture"

[ $fail -eq 0 ] || exit 1
echo "PASS: rule modules read as tcalc programs: readings, refusals, rulec against the reading (lambda, Megalodon)"
