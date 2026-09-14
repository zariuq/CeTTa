#!/usr/bin/env bash
set -euo pipefail

mode=${1:?usage: gen_rule_slot_view_family.sh MODE ITERATIONS}
iterations=${2:?usage: gen_rule_slot_view_family.sh MODE ITERATIONS}

case "$iterations" in
    ''|*[!0-9]*)
        printf 'ITERATIONS must be a nonnegative integer\n' >&2
        exit 2
        ;;
esac

case "$mode" in
    sibling)
        pattern='(pair $x $x)'
        value='(pair alpha alpha)'
        ;;
    nested)
        pattern='(nest (left $x) (right $x))'
        value='(nest (left beta) (right beta))'
        ;;
    braided)
        pattern='(braid $x (link $y $x) $y)'
        value='(braid gamma (link delta gamma) delta)'
        ;;
    evidence)
        pattern='(evidence (tag $x) (payload $x $x))'
        value='(evidence (tag epsilon) (payload epsilon epsilon))'
        ;;
    structural)
        pattern='(tree (node $x $y) (node $y $x))'
        value='(tree (node zeta eta) (node eta zeta))'
        ;;
    fanout)
        pattern='(fan $x $x $x $x $x $x $x $x $x $x $x $x $x $x $x $x)'
        value='(fan theta theta theta theta theta theta theta theta theta theta theta theta theta theta theta theta)'
        ;;
    *)
        printf 'unknown rule-slot family: %s\n' "$mode" >&2
        exit 2
        ;;
esac

# Four ordered occurrences remain simultaneously eligible through the proof
# argument.  Every family has a different nonlinear term geometry, while the
# runtime sees only ordinary source-derived pattern plans.  One occurrence
# recurs; the other three take an authored empty branch until the base case.
for lane in one two three four; do
    printf '(= (rule-slot-walk $n (%s $proof) %s)\n' "$lane" "$pattern"
    printf '   (if (> $n 0)\n'
    if [ "$lane" = one ]; then
        printf '       (rule-slot-walk (- $n 1) $next %s)\n' "$value"
    else
        printf '       (empty)\n'
    fi
    if [ "$lane" = one ]; then
        printf '       done))\n'
    else
        printf '       never))\n'
    fi
done

printf '!(rule-slot-walk %s $proof %s)\n' "$iterations" "$value"
