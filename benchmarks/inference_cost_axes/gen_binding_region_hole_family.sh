#!/usr/bin/env bash
set -euo pipefail

mode=${1:?usage: gen_binding_region_hole_family.sh MODE ITERATIONS}
iterations=${2:?usage: gen_binding_region_hole_family.sh MODE ITERATIONS}

case "$iterations" in
    ''|*[!0-9]*)
        printf 'ITERATIONS must be a nonnegative integer\n' >&2
        exit 2
        ;;
esac

printf '(= (binding-id $x) $x)\n'
printf '(= (binding-box $x) (Box $x))\n'
printf '(= (binding-pair $x) (Pair $x $x))\n'

case "$mode" in
    linear)
        body='(let* (($next (binding-id (- $n 1))))
                (binding-region-walk $next))'
        ;;
    nested)
        body='(let* (((Box $next) (binding-box (- $n 1))))
                (binding-region-walk $next))'
        ;;
    dependent)
        body='(let* (($previous (binding-id (- $n 1)))
                      ($next (binding-id $previous)))
                (binding-region-walk $next))'
        ;;
    repeated)
        body='(let* (((Pair $next $next) (binding-pair (- $n 1))))
                (binding-region-walk $next))'
        ;;
    anonymous)
        body='(let* (($_ (binding-id $n))
                      ($next (binding-id (- $n 1))))
                (binding-region-walk $next))'
        ;;
    wide)
        body='(let* (($a (binding-id (- $n 1)))
                      ($b (binding-id $a))
                      ($c (binding-id $b))
                      ($d (binding-id $c))
                      ($e (binding-id $d))
                      ($f (binding-id $e))
                      ($g (binding-id $f))
                      ($next (binding-id $g)))
                (binding-region-walk $next))'
        ;;
    *)
        printf 'unknown binding Region/Hole family: %s\n' "$mode" >&2
        exit 2
        ;;
esac

printf '(= (binding-region-walk $n)\n'
printf '   (if (== $n 0)\n'
printf '       done\n'
printf '       %s))\n' "$body"
printf '!(binding-region-walk %s)\n' "$iterations"
