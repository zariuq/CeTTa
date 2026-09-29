; A late error is observable even when a later equation has a shallow answer.
(= (deep-boom $n) (if (> $n 0) (deep-boom (- $n 1)) (/ 1 0)))
(= (candidate) (deep-boom 20000))
(= (candidate) ok)
!(once (candidate))
