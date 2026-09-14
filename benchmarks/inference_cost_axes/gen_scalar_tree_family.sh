#!/usr/bin/env bash
set -euo pipefail

mode=${1:?usage: gen_scalar_tree_family.sh MODE ITERATIONS [LAYERS]}
iterations=${2:?usage: gen_scalar_tree_family.sh MODE ITERATIONS [LAYERS]}
layers=${3:-1}

case "$layers" in
    ''|*[!0-9]*|0)
        printf 'LAYERS must be a positive integer\n' >&2
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
        printf 'unknown scalar-tree family: %s\n' "$mode" >&2
        exit 2
        ;;
esac

# Grow the same candidate-local algebra without changing its open-call or
# recursive boundaries.  This exposes whether a Region realization follows
# authored program size rather than a hidden small-expression ceiling.
layer=1
while [ "$layer" -lt "$layers" ]; do
    case "$mode" in
        arithmetic)
            condition="(and $condition (> (- (+ (* \$n 2) $layer) $layer) 0))"
            ;;
        conjunction)
            condition="(and $condition (not (< \$n 0)))"
            ;;
        mixed)
            condition="(and $condition (>= (* (+ \$n 0.5) 2) 1.0))"
            ;;
        structural)
            condition="(and $condition (== (+ $layer 1) (+ 1 $layer)))"
            ;;
        disjunction)
            condition="(and $condition (or (< \$n 0) (numeric-eq (+ \$n $layer) (+ \$n $layer.0))))"
            ;;
    esac
    layer=$((layer + 1))
done

printf '%s\n' \
    '(= (scalar-family-open keep) ok)' \
    '(= (scalar-family-open drop) (empty))' \
    '' \
    '(= (scalar-family-walk $n keep)' \
    '   (let $_ (scalar-family-open keep)'
printf '     (if %s\n' "$condition"
printf '%s\n' \
    '         (scalar-family-walk (- $n 1) keep)' \
    '         done)))' \
    '(= (scalar-family-walk $n drop)' \
    '   (let $_ (scalar-family-open drop)'
printf '     (if %s\n' "$condition"
printf '%s\n' \
    '         (scalar-family-walk (- $n 1) drop)' \
    '         never)))'
printf '!(scalar-family-walk %s $lane)\n' "$iterations"
