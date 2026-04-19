; Regression: the shallow dispatch fast path must not bypass equations that can
; still match the symbol head. The counter witness should stay positive here,
; because this call really does need call-term observation.

(= (project2 $x $y)
   (Pair $x $y))

(= (pair-via-eq $ss)
   (match $ss (: $name $body)
     (match $ss (: $other $body2)
       (project2 $body $body2))))

!(bind! &ss (new-space))
!(add-atom &ss (: ax-1 (P $p $q)))
!(add-atom &ss (: ax-2 (Q $r $s)))

!(pragma! outcome-variant-sharing on)
!(assertAlphaEqualToResult
  (pair-via-eq &ss)
  ((Pair (P $a $b) (P $c $d))
   (Pair (P $a $b) (Q $e $f))
   (Pair (Q $g $h) (P $i $j))
   (Pair (Q $g $h) (Q $k $l))))

!(reset-runtime-stats!)

!(assertAlphaEqualToResult
  (pair-via-eq &ss)
  ((Pair (P $a $b) (P $c $d))
   (Pair (P $a $b) (Q $e $f))
   (Pair (Q $g $h) (P $i $j))
   (Pair (Q $g $h) (Q $k $l))))

!(bind! &stats (runtime-stats!))

!(assertEqualToResult
  (match &stats
         (runtime-counter outcome-variant-materialize-dispatch-call-term $n)
         (> $n 0))
  (True))
