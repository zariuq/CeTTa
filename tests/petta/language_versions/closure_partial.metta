; A closure parameter applied to fewer arguments than its arrow takes is
; the arrow over the remaining parameters, with the same effect.
(: map-each (-[$e]-> (-[$e]-> $a $b) (List $a) (List $b)))
(= (map-each $f ()) ())
(= (map-each $f (cons $x $xs)) (cons ($f $x) (map-each $f $xs)))

(: map-with (-[$e]-> (-[$e]-> $a $b $c) $a (List $b) (List $c)))
(= (map-with $f $x $ys) (map-each ($f $x) $ys))

(: add (-[det]-> Number Number Number))
(= (add $x $y) (+ $x $y))

!(test (map-with add 1 (cons 2 (cons 3 ()))) (3 4))
