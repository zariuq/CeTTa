(: foo (-> Number Number))
(: foo (-> Number Number Number))

(= (foo $x) $x)
(= (foo $x $y) (+ $x $y))

!(assertEqual (foo 1) 1)
!(assertEqual (foo 1 2) 3)
