; A function whose body is a partial application, called with exactly the
; arguments it is written with, answers a value that a call can apply.
(= (add-to $x) (+ $x))
(= (twice-of $f $x) ($f ($f $x)))
!(test (twice-of (add-to 1) 5) 7)
!(test (let $g (add-to 10) ($g 5)) 15)
!(test ((add-to 1) 2) 3)
