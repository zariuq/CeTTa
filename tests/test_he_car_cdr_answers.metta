; HE's car-atom and cdr-atom are library equations whose answers HE
; evaluates: an element or a tail that could evaluate further is evaluated
; where it is answered, and one that cannot is answered as it is.

(= (f $x) (Called $x))
!(car-atom ((f g) h))
!(cdr-atom (h f g))
!(car-atom (1 2 3))
!(cdr-atom (1 2 3))
!(car-atom (a b))
!(cdr-atom (a))

; A collected tuple's head and tail.
(= (collected-head) (let $c (collapse (superpose ((f g) h))) (car-atom $c)))
!(collected-head)
(= (collected-tail) (let $c (collapse (superpose (h f g))) (cdr-atom $c)))
!(collected-tail)

; Through a let and a recursive walk.
!(let $t (cdr-atom (h f g)) $t)
(= (heads $l) (if (== $l ()) () (let $h (car-atom $l) (let $r (heads (cdr-atom $l)) (cons-atom $h $r)))))
!(heads (1 2 3))
