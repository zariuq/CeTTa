#!/bin/sh
# Ordered ladder checks. CeTTa, the independent oracle, and SWI-PeTTa
# must print the same answers. This script does not time anything.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cetta=${CETTA:-$root/cetta}
ladder=$root/benchmarks/symbolic_ladder
swi=${PETTA_RUN:-$root/../PeTTa/run.sh}
if [ ! -x "$cetta" ]; then
    echo "missing $cetta" >&2
    exit 1
fi
oracle=$(mktemp)
python3 "$ladder/oracle.py" > "$oracle"
fail=0
for stem in \
    graph_count_small graph_count_duplicate graph_count_miss \
    graph_witness_small graph_shapes hm_small parse_small reach_small \
    join_caller_bind join_mutate join_mutate_template \
    handoff_if handoff_open residual_alias rollback_branch \
    fresh_pick graph_bound fold_arith \
    hm_n8 hm_n16 hm_n32 hm_holdout12 \
    star_n6 star_n8 star_holdout7 tri_n6 tri_holdout7 \
    dia_n5 dia_holdout4 hyper_holdout \
    parse_n8 parse_n16 parse_n32 parse_holdout7 \
    reach_n8 reach_n16 reach_n32 reach_holdout24
do
    actual=$("$cetta" --lang petta "$ladder/$stem.metta")
    expected=$(cat "$ladder/$stem.expected")
    swi_out=$("$swi" "$ladder/$stem.metta" --silent)
    ora=$(awk -v name="$stem" '
        $0 == "## " name { take=1; next }
        take && /^## / { exit }
        take { print }
    ' "$oracle")
    # Drop the blank line awk keeps after the section.
    ora=$(printf '%s\n' "$ora" | sed -e :a -e '/^\n*$/{$d;N;ba' -e '}')
    if [ "$actual" != "$expected" ]; then
        echo "FAIL cetta $stem" >&2
        fail=1
    elif [ "$swi_out" != "$expected" ]; then
        echo "FAIL swi $stem" >&2
        fail=1
    elif [ "$ora" != "$expected" ]; then
        echo "FAIL oracle $stem" >&2
        printf 'oracle [%s]\nexpected [%s]\n' "$ora" "$expected" >&2
        fail=1
    else
        echo "ok $stem"
    fi
done
for numeric_stem in he_numbers he_number_boundaries; do
    he_actual=$("$cetta" --lang he "$ladder/$numeric_stem.metta")
    he_expected=$(cat "$ladder/$numeric_stem.he.expected")
    petta_numbers=$("$cetta" --lang petta "$ladder/$numeric_stem.metta")
    petta_numbers_expected=$(cat "$ladder/$numeric_stem.petta.expected")
    if [ "$he_actual" != "$he_expected" ]; then
        echo "FAIL he numbers $numeric_stem" >&2
        printf 'actual [%s]\nexpected [%s]\n' "$he_actual" "$he_expected" >&2
        fail=1
    elif [ "$petta_numbers" != "$petta_numbers_expected" ]; then
        echo "FAIL petta numbers stay distinct $numeric_stem" >&2
        printf 'actual [%s]\nexpected [%s]\n' "$petta_numbers" "$petta_numbers_expected" >&2
        fail=1
    else
        echo "ok $numeric_stem"
    fi
done
norm() { sed -E 's/<space 0x[0-9a-fA-F]+>/<space>/g'; }
for stem in fold_arith fold_arith_boundary fold_arith_explode fold_arith_amb fold_arith_mod_column fold_arith_literal_alt fold_arith_repeated_alt; do
    fast=$( "$cetta" --lang petta "$ladder/$stem.metta" | norm )
    ref=$( CETTA_FOLD_ARITH_REFERENCE=1 "$cetta" --lang petta "$ladder/$stem.metta" | norm )
    if [ "$stem" != fold_arith ]; then
        expected=$(norm < "$ladder/$stem.expected")
        if [ "$fast" != "$expected" ]; then
            echo "FAIL fold boundary $stem" >&2
            printf 'fast [%s]\nexpected [%s]\n' "$fast" "$expected" >&2
            fail=1
            continue
        fi
    fi
    if [ "$fast" != "$ref" ]; then
        echo "FAIL fold reference $stem" >&2
        printf 'fast [%s]\nref [%s]\n' "$fast" "$ref" >&2
        fail=1
    else
        echo "ok fold-reference $stem"
    fi
done
rm -f "$oracle"

# Finite fuel is a completion status. A logical miss completes with
# count 0. The same budget on a diverging equation does not.
fuel_dir=$(mktemp -d)
trap 'rm -rf "$fuel_dir"' EXIT
cat > "$fuel_dir/miss.metta" << 'EOF'
!(match &self (absent $x) $x)
EOF
cat > "$fuel_dir/loop.metta" << 'EOF'
(= (loop $n) (loop (+ $n 1)))
!(loop 0)
EOF
cat > "$fuel_dir/ok.metta" << 'EOF'
!(+ 1 1)
EOF
miss_out=$("$cetta" --lang petta --count-only "$fuel_dir/miss.metta" 2>"$fuel_dir/miss.err") || true
if [ "$miss_out" != "0" ] || [ -s "$fuel_dir/miss.err" ]; then
    echo "FAIL fuel logical miss" >&2
    fail=1
else
    echo "ok fuel-logical-miss"
fi
"$cetta" --lang petta --count-only --fuel 8 "$fuel_dir/loop.metta" >"$fuel_dir/loop-count.out" 2>"$fuel_dir/loop-count.err" || loop_count_rc=$?
loop_count_rc=${loop_count_rc:-0}
if [ "$loop_count_rc" -eq 0 ] || [ -s "$fuel_dir/loop-count.out" ] ||
    ! grep -qx 'error: count observation incomplete: fuel-exhausted' "$fuel_dir/loop-count.err"; then
    echo "FAIL fuel count exhaustion" >&2
    fail=1
else
    echo "ok fuel-count-exhausted"
fi
"$cetta" --lang petta --fuel 8 "$fuel_dir/loop.metta" >"$fuel_dir/loop-ans.out" 2>"$fuel_dir/loop-ans.err" || loop_ans_rc=$?
loop_ans_rc=${loop_ans_rc:-0}
if [ "$loop_ans_rc" -eq 0 ] || [ -s "$fuel_dir/loop-ans.out" ] ||
    ! grep -qx 'error: observation incomplete: fuel-exhausted' "$fuel_dir/loop-ans.err"; then
    echo "FAIL fuel answer exhaustion" >&2
    fail=1
else
    echo "ok fuel-answer-exhausted"
fi
ok_out=$("$cetta" --lang petta --fuel 1000 "$fuel_dir/ok.metta" 2>"$fuel_dir/ok.err") || true
if [ "$ok_out" != "2" ] || [ -s "$fuel_dir/ok.err" ]; then
    echo "FAIL fuel sufficient query" >&2
    printf 'out [%s]\n' "$ok_out" >&2
    fail=1
else
    echo "ok fuel-sufficient"
fi
exit "$fail"
