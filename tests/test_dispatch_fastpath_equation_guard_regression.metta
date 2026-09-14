; Regression: outcome-variant sharing must not change calls whose symbol head
; can still match an equation.  Compare the observable result with sharing off
; and on; the particular materialization route is a benchmark receipt.

(= (project2 $x $y)
   (Pair $x $y))

(= (pair-via-eq $ss)
   (match $ss (: $name $body)
     (match $ss (: $other $body2)
       (project2 $body $body2))))

!(bind! &ss (new-space))
!(add-atom &ss (: ax-1 (P $p $q)))
!(add-atom &ss (: ax-2 (Q $r $s)))

!(pragma! outcome-variant-sharing off)
!(assertAlphaEqualToResult
  (pair-via-eq &ss)
  ((Pair (P $a $b) (P $c $d))
   (Pair (P $a $b) (Q $e $f))
   (Pair (Q $g $h) (P $i $j))
   (Pair (Q $g $h) (Q $k $l))))

!(pragma! outcome-variant-sharing on)
!(assertAlphaEqualToResult
  (pair-via-eq &ss)
  ((Pair (P $a $b) (P $c $d))
   (Pair (P $a $b) (Q $e $f))
   (Pair (Q $g $h) (P $i $j))
   (Pair (Q $g $h) (Q $k $l))))
