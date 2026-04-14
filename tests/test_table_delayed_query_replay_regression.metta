; Regression: cached variant-table query replay must feed open-term answers back
; through the evaluator without collapsing to the canonical stored RHS.

(= (project2 $x $y)
   (Pair $x $y))

(= (pair-via-eq $ss)
   (match $ss (: $name $body)
     (match $ss (: $other $body2)
       (project2 $body $body2))))

!(bind! &ss (new-space))
!(add-atom &ss (: ax-1 (P $p $q)))
!(add-atom &ss (: ax-2 (Q $r $s)))

!(let* (
    (() (pragma! search-table-mode variant))
    (() (assertAlphaEqualToResult
          (pair-via-eq &ss)
          ((Pair (P $a $b) (P $c $d))
           (Pair (P $a $b) (Q $e $f))
           (Pair (Q $g $h) (P $i $j))
           (Pair (Q $g $h) (Q $k $l)))))
    (() (reset-runtime-stats!))
    (() (assertAlphaEqualToResult
          (pair-via-eq &ss)
          ((Pair (P $a $b) (P $c $d))
           (Pair (P $a $b) (Q $e $f))
           (Pair (Q $g $h) (P $i $j))
           (Pair (Q $g $h) (Q $k $l)))))
    ($stats (runtime-stats!))
    (() (assertEqualToResult
          (match $stats (runtime-counter table-hit $n) (> $n 0))
          (True)))
  )
  ())
