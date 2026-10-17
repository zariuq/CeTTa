; collapse collects each answer as the value it is: an improper or open cons
; cell stays a cell, as SWI-PeTTa's findall copy keeps [H|T]; printing writes
; it as (cons H T).
!(collapse (cons 1 foo))
!(let $l (collapse (cons 1 foo)) (let ($x) $l (union-atom $x (3))))
!(let $l (collapse (cons 1 (cons 2 ()))) (let ($x) $l (union-atom $x (3))))
!(let $l (collapse (superpose ((cons 1 foo) (cons 2 ())))) (length $l))
!(let $l (collapse (cons 1 foo)) (let ($x) $l (cons-atom 0 $x)))
!(let $l (collapse (cons 1 foo)) (let ($x) $l (car-atom $x)))
!(let $l (collapse (cons 1 foo)) (let ($x) $l (cdr-atom $x)))
!(let $l (collapse (cons 1 foo)) (let ($x) $l $x))
!(let $l (collapse (wrap (cons 1 foo))) (let ((wrap $x)) $l (car-atom $x)))
(= (cell $h) (cons $h foo))
!(let $l (collapse (cell 7)) (let ($x) $l (cdr-atom $x)))
!(let $l (collapse (superpose ((cons a foo) (cons b bar)))) (let ($x $y) $l (union-atom (car-atom $x) (car-atom $y))))
!(let $l (collapse (cons 1 foo)) (let ($x) $l (== $x (cons 1 foo))))
!(let $l (collapse (cons 1 $t)) (let ($x) $l (once (union-atom $x (3)))))
