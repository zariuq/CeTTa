; A function of no argument that answers a value to apply, called with the
; arguments for that value.
(= (operator) (+))
!(test (operator 1 1) 2)

(= (compose $f $g $x) ($f ($g $x)))
(= (add-then-double) (compose (* 2) (+ 1)))
!(test (add-then-double 1) 4)
