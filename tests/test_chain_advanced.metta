; Advanced chain operations

; Chain with arithmetic
!(assertEqual (chain (+ 10 20) $r (result $r)) (result 30))

; Chain with equation evaluation
(= (double $x) (* $x 2))
!(assertEqual (chain (double 5) $r (got $r)) (got 10))

; Nested chain
!(assertEqual (chain (+ 1 2) $x (chain (+ $x 10) $y (pair $x $y))) (pair 3 13))
