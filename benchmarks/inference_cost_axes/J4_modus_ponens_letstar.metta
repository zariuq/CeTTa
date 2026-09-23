; J4: modus-ponens shape with two recursive calls sharing bindings through
; let*.  Size ladder: change the budget (3, 5, 8).
(= (j4-gtz $s (: ax T)) (Mk 1 ax))
(= (j4-gtz $s (: (mp $f $x) T))
   (if (< 2 $s)
       (let* (((Mk $fs $f) (j4 (- $s 2) (: $f T)))
              ((Mk $xs $x) (j4 (- (- $s 1) $fs) (: $x T))))
         (Mk (+ (+ $fs $xs) 1) (mp $f $x)))
       none))
(= (j4 $s (: $x $a))
   (if (< 0 $s)
       (j4-gtz $s (: $x $a))
       none))
!(j4 12 (: (mp ax ax) T))
