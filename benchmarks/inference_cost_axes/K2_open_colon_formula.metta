; K2: leftover dest against interned colon-tagged open type, then mp.
; Loowoz's ax₂/mp shape: the formula is ground, the proof dest is open.
(= (k2-gtz $s (: ax (→ $a (→ $b $a)))) (Mk 1 ax))
(= (k2-gtz $s (: (mp $f $x) $b))
   (if (< 2 $s)
       (let* (((Mk $fs $f) (k2 (- $s 2) (: $f (→ $a $b))))
              ((Mk $xs $x) (k2 (- (- $s 1) $fs) (: $x $a))))
         (Mk (+ (+ $fs $xs) 1) (mp $f $x)))
       (empty)))
(= (k2 $s (: $x $a))
   (if (< 0 $s)
       (k2-gtz $s (: $x $a))
       (empty)))
!(k2 5 (: $p (→ q (→ r q))))
