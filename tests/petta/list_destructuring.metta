; Destructuring a list: decons-atom, car-atom and cdr-atom on values whose
; elements were bound after the list was built, nested and repeated patterns,
; and lists built with cons.
(= (walk $xs)
   (if (== $xs ()) ()
       (let ($h $t) (decons-atom $xs) (cons-atom (w $h) (walk $t)))))
!(walk (1 2 3))

; elements bound after the list is built
(= (late $x $y) (let $l ($x b $y) (let ($x $y) (a c) (walk $l))))
!(late $p $q)
(= (late-walk $x) (let $l ($x 2) (let $x (k 1) (walk $l))))
!(late-walk $p)
(= (late-car $x) (let $l ($x 2) (let $x 1 (car-atom $l))))
!(late-car $p)
(= (late-cdr $x) (let $l (0 $x) (let $x (n 7) (cdr-atom $l))))
!(late-cdr $p)

; nested and repeated patterns
(= (nested $xs) (let (($a $b) $t) (decons-atom $xs) (got $a $b $t)))
!(nested ((1 2) 3 4))
!(nested (1 2))
(= (same $xs) (let ($h $h) (decons-atom $xs) (same-as $h)))
!(same (1 1))
!(same ((1) 1))
!(same (1 2))

; no answer for () or a value that is no expression
(= (first-of $xs) (let ($h $t) (decons-atom $xs) $h))
!(collapse (first-of ()))
!(collapse (first-of 5))
!(car-atom ())
!(cdr-atom (1))

; a list built with cons and read back
(= (consed $x) (let $l (cons-atom $x (cons-atom 2 ())) (walk $l)))
!(consed 1)

; a variable left unbound inside the list stays a variable
(= (open-walk $x) (size-atom (walk (1 $x 3))))
!(open-walk $u)

; a list built with cons, read where a variable holds it: chains print as lists
(= (carrier-walk $x) (let $l ($x 9) (let $x (cons 1 (cons 2 ())) (walk $l))))
!(carrier-walk $p)
(= (carrier-car $x) (let $l ($x 9) (let $x (cons 1 (cons 2 ())) (car-atom $l))))
!(carrier-car $p)
(= (carrier-tail $x) (let $l (0 $x) (let $x (cons 1 ()) (cdr-atom $l))))
!(carrier-tail $p)
(= (carrier-direct $x) (walk (cons 1 (cons 2 ()))))
!(carrier-direct 0)
!(cons 1 (cons 2 ()))
