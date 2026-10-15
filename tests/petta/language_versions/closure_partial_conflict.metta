; The partial application returns Bool; the declared list of numbers
; contradicts it.
(: map-each (-[$e]-> (-[$e]-> $a $b) (List $a) (List $b)))
(= (map-each $f ()) ())
(= (map-each $f (cons $x $xs)) (cons ($f $x) (map-each $f $xs)))

(: map-with (-[$e]-> (-[$e]-> $a $b Bool) $a (List $b) (List Number)))
(= (map-with $f $x $ys) (map-each ($f $x) $ys))
