; Advanced chain operations

; Chain with arithmetic
!(assertEqual (chain (+ 10 20) $r (result $r)) (result 30))

; Chain with equation evaluation
(= (double $x) (* $x 2))
!(assertEqual (chain (double 5) $r (got $r)) (got 10))

; Nested chain
!(assertEqual (chain (+ 1 2) $x (chain (+ $x 10) $y (pair $x $y))) (pair 3 13))

; Instantiation replaces each original occurrence once, without equating
; the destination with its own open replacement syntax.
!(assertEqualToResult
  (chain (quote (f $x)) $x (quote (g $x)))
  ((quote (g (quote (f $x))))))
!(assertEqualToResult
  (let $x (quote (f $x)) (quote (g $x))) ())
