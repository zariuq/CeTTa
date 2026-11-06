; Rows that repeat one type are one declaration: a call answers once for
; them, through a variable too, and get-type answers the type once.
(: myf (-> Number Number))
(: myf (-> Number Number))
(= (myf $x) (+ $x 1))
(= (app $g $y) ($g $y))
!(collapse (app myf 1))
!(collapse (myf 1))
!(collapse (get-type myf))

; A lambda called through a variable, in a program whose only repeated rows
; repeat one type.
(= (loop $n $f $acc) (if (== $n 0) $acc (loop (- $n 1) $f ($f $acc))))
!(loop 100 (|-> ($x) (+ $x 1)) 0)
!(collapse (app (|-> ($x) (myf $x)) 5))
