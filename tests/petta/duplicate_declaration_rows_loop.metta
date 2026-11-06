; Rows that repeat one type are one declaration, so they leave a lambda
; called through a variable on the compiled tier.
(: myf (-> Number Number))
(: myf (-> Number Number))
(= (loop $n $f $acc) (if (== $n 0) $acc (loop (- $n 1) $f ($f (+ $acc 0)))))
(= (go $n) (loop $n (|-> ($x) (+ $x 1)) 0))
!(go 1000)
