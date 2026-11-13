; A function whose body is a partial application takes its further arguments.
(= (add-to $x) (+ $x))
(= (plus) (+))
!(test (add-to 1 2) 3)
!(test (plus 1 1) 2)

; The same with a function of the program as the one applied in part.
(= (three $a $b $c) (triple $a $b $c))
(= (two-given $a $b) (three $a $b))
!(test (two-given 1 2 3) (triple 1 2 3))

; A declaration with fewer inputs than the call has arguments does not describe it.
(: curried (-> Number (-> Number Number)))
(= (curried $x) (+ $x))
!(test (curried 1 2) 3)

; A body that is a whole call is not widened, and a function still runs backwards.
(= (whole $a $b) (+ $a $b))
!(test (whole 1 2) 3)
!(test ((whole 1) 2) 3)
(= (plus-of Z $y) $y)
(= (plus-of (S $x) $y) (S (plus-of $x $y)))
!(test (let (plus-of $a (S Z)) (S (S Z)) $a) (S Z))
