; Negative mirror: the tail of a List Number cons must itself be List Number,
; not Number.

(: prepend-number (-[det]-> Number (List Number)))
(= (prepend-number $tail) (cons 1 $tail))
