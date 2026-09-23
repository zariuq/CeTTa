; K1: J4's modus-ponens let* with an *open* proof dest, as Loowoz queries
; `(: $x formula)` rather than a ground `(mp ax ax)`.  Leftover dest `$p`
; binds interned `(mp $f $x)`; the body observes `$f` and `$x` through two
; recursive calls.  Size ladder: budget 3, 5, 8.
(= (k1-gtz $s (: ax T)) (Mk 1 ax))
(= (k1-gtz $s (: (mp $f $x) T))
   (if (< 2 $s)
       (let* (((Mk $fs $f) (k1 (- $s 2) (: $f T)))
              ((Mk $xs $x) (k1 (- (- $s 1) $fs) (: $x T))))
         (Mk (+ (+ $fs $xs) 1) (mp $f $x)))
       (empty)))
(= (k1 $s (: $x $a))
   (if (< 0 $s)
       (k1-gtz $s (: $x $a))
       (empty)))
!(k1 5 (: $p T))
