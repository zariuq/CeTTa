; This shallow composition must preserve its alpha-distinct outcomes with
; outcome-variant sharing disabled or enabled.  Materialization choices are
; measured separately rather than made part of the language contract.

(= (pair-body $a1 $a2)
   (Pair $a1 $a2))

(= (pair-from-ss $ss)
   (match $ss (: $x1 $a1)
     (match $ss (: $x2 $a2)
       (: ($x1 $x2) (Pair $a1 $a2)))))

!(bind! &ss (new-space))
!(add-atom &ss (: ax-1 (P $p $q)))
!(add-atom &ss (: ax-2 (Q $r $s)))

!(pragma! outcome-variant-sharing off)
!(assertAlphaEqualToResult
  (pair-from-ss &ss)
  ((: (ax-1 ax-1) (Pair (P $a $b) (P $c $d)))
   (: (ax-1 ax-2) (Pair (P $a $b) (Q $e $f)))
   (: (ax-2 ax-1) (Pair (Q $g $h) (P $i $j)))
   (: (ax-2 ax-2) (Pair (Q $g $h) (Q $k $l)))))

!(pragma! outcome-variant-sharing on)
!(assertAlphaEqualToResult
  (pair-from-ss &ss)
  ((: (ax-1 ax-1) (Pair (P $a $b) (P $c $d)))
   (: (ax-1 ax-2) (Pair (P $a $b) (Q $e $f)))
   (: (ax-2 ax-1) (Pair (Q $g $h) (P $i $j)))
   (: (ax-2 ax-2) (Pair (Q $g $h) (Q $k $l)))))
