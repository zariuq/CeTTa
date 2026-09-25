;; A function given fewer arguments than any of its equations takes is a
;; partial application: a value that prints as (partial f (args)) and is
;; called when it heads an application with the remaining arguments.

(= (add $a $b) (+ $a $b))
(= (add3 $a $b $c) (+ $a (+ $b $c)))
(= (use $f) ($f 10))
(= (twice $f $x) ($f ($f $x)))
(fact 1)

!(add 1)
!(add3 1)
!(add3 1 2)
!(collapse (add 1))
!(let $p (add3 1) ($p 2 3))
!(let $p (add3 1) (let $q ($p 2) ($q 3)))
!(use (add 1))
!(twice (add 3) 4)
!((add3 1 2) 3)
!(once (add 1))
!(superpose ((add 1) (add 2)))
