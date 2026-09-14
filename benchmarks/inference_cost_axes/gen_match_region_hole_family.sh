#!/usr/bin/env bash
set -euo pipefail

mode=${1:?usage: gen_match_region_hole_family.sh MODE ITERATIONS}
iterations=${2:?usage: gen_match_region_hole_family.sh MODE ITERATIONS}

case "$iterations" in
    ''|*[!0-9]*)
        printf 'ITERATIONS must be a nonnegative integer\n' >&2
        exit 2
        ;;
esac

case "$mode" in
    arithmetic)
        condition='(> (- (+ (* $n 2) 1) 1) 0)'
        ;;
    conjunction)
        condition='(and (> $n 0) (not (< $n 0)))'
        ;;
    mixed)
        condition='(and (> $n 0) (>= (* (+ $n 0.5) 2) 1.0))'
        ;;
    structural)
        condition='(and (> $n 0) (== (+ 1 1) (* 1 2)))'
        ;;
    disjunction)
        condition='(and (> $n 0)
                        (or (< $n 0)
                            (numeric-eq (+ $n 1) (+ $n 1.0))))'
        ;;
    *)
        printf 'unknown match/region family: %s\n' "$mode" >&2
        exit 2
        ;;
esac

# Four simultaneously matchable, ordered occurrences isolate the generic
# match -> deterministic Region -> open Hole boundary.  Exactly one occurrence
# recurs; the other three select an authored empty hole.  The shape is shared
# by five independent scalar algebras and grants no relation name special
# authority.
for lane in one two three four; do
    printf '(= (region-hole-walk $n (%s (-> (-> $a (-> $b $c)) (-> (-> $d $e) (-> $f $g)))))\n' "$lane"
    printf '   (if %s\n' "$condition"
    if [ "$lane" = one ]; then
        printf '       (region-hole-walk (- $n 1) $next)\n'
        printf '       done))\n'
    else
        printf '       (empty)\n'
        printf '       never))\n'
    fi
done

printf '!(region-hole-walk %s $proof)\n' "$iterations"
