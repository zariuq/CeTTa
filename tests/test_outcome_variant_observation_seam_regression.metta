; Regression: the current composition witness still forces delayed variants
; primarily through tuple interpretation, while the shallow dispatch fast path
; avoids eager call-term observation for obviously data-like tuples.

(= (pair-from-space $ss)
   (match $ss (: $x1 $a1)
     (match $ss (: $x2 $a2)
       (: ($x1 $x2) (Pair $a1 $a2)))))

!(bind! &ss (new-space))
!(add-atom &ss (: ax-1 (P $p $q)))
!(add-atom &ss (: ax-2 (Q $r $s)))

!(pragma! outcome-variant-sharing on)
!(reset-runtime-stats!)
!(assertAlphaEqualToResult
  (pair-from-space &ss)
  ((: (ax-1 ax-1) (Pair (P $a $b) (P $c $d)))
   (: (ax-1 ax-2) (Pair (P $a $b) (Q $e $f)))
   (: (ax-2 ax-1) (Pair (Q $g $h) (P $i $j)))
   (: (ax-2 ax-2) (Pair (Q $g $h) (Q $k $l)))))

!(bind! &stats (runtime-stats!))
!(assertEqualToResult
  (match &stats
         (runtime-counter outcome-variant-prefix-compact $n)
         (== $n 0))
  (True))
!(assertEqualToResult
  (match &stats
         (runtime-counter outcome-variant-slot-sink $n)
         (== $n 0))
  (True))
!(assertEqualToResult
  (match &stats
         (runtime-counter outcome-variant-materialize-interpret-tuple $n)
         (> $n 0))
  (True))
!(assertEqualToResult
  (match &stats
         (runtime-counter outcome-variant-materialize-dispatch-call-term $n)
         (== $n 0))
  (True))
