#!/bin/sh
# notations.sh BIN DIR: the three notations say the same programs.
#  - Each .tcalc fixture that a notation can write is written in it, read back,
#    and run: its output must be the fixture's .expected.
#  - Each .rho and .chr fixture makes a round trip, notation -> tcalc ->
#    notation -> tcalc: the two tcalc programs must run alike.
# A fixture a notation cannot write is counted, not failed (the writer says why).
bin=$1
dir=$2
mkdir -p "$dir"
fail=0; same=0; refused=0
run() {   # run FILE PROFILE: the program's output
    if [ -n "$2" ]; then "$bin" --lang tcalc --profile "$2" "$1" 2>&1
    else "$bin" --lang tcalc "$1" 2>&1; fi
}
translate() {   # translate FROM TO FILE
    "$bin" --translate --lang tcalc --syntax "$1" --lang tcalc --syntax "$2" "$3"
}
merge() {   # merge BASE OVER: the settings of BASE that OVER does not set, then OVER
    over=$(echo "$2" | tr ',' ';'); out=""
    for s in $(echo "$1" | tr ';,' '  '); do
        case ";$over;" in *";${s%%=*}="*) ;; *) out="$out${out:+;}$s" ;; esac
    done
    [ -n "$over" ] && out="$out${out:+;}$over"
    echo "$out"
}
for f in tests/tcalc/*.tcalc; do
    case "$f" in */refused_*) continue ;; esac
    profile=$(sed -n '1s/^; profile: //p' "$f")
    for n in chr rho; do
        if ! translate tcalc "$n" "$f" > "$dir/written.$n" 2>/dev/null; then
            refused=$((refused + 1)); continue
        fi
        translate "$n" tcalc "$dir/written.$n" > "$dir/back.tcalc" || { echo "FAIL: $f in $n reads back"; fail=$((fail + 1)); continue; }
        # f1r3lang has no declarations: the program's own become the written
        # file's profile comment, which the fixture's profile overrides
        written=$(sed -n '1s/^\/\/ profile: //p' "$dir/written.$n")
        if [ "$(run "$dir/back.tcalc" "$(merge "$written" "$profile")")" = "$(cat "${f%.tcalc}.expected")" ]; then
            same=$((same + 1))
        else
            echo "FAIL: $f written in $n runs otherwise"; fail=$((fail + 1))
        fi
    done
done
for f in tests/tcalc/*.rho tests/tcalc/*.chr; do
    case "$f" in */refused_*) continue ;; esac
    n=${f##*.}
    profile=$(sed -n '1s/^\(\/\/\|%\) profile: //p' "$f")
    translate "$n" tcalc "$f" > "$dir/one.tcalc" &&
        translate tcalc "$n" "$dir/one.tcalc" > "$dir/again.$n" &&
        translate "$n" tcalc "$dir/again.$n" > "$dir/two.tcalc" || { echo "FAIL: $f round trip"; fail=$((fail + 1)); continue; }
    if [ "$(run "$dir/one.tcalc" "$profile")" = "$(run "$dir/two.tcalc" "$profile")" ]; then
        same=$((same + 1))
    else
        echo "FAIL: $f runs otherwise after a round trip"; fail=$((fail + 1))
    fi
done
rm -rf "$dir"
[ $fail -eq 0 ] || exit 1
echo "PASS: tcalc notations: $same written or round-tripped programs run alike ($refused writings refused)"
