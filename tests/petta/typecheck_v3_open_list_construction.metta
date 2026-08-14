(: empty-numbers (-[det]-> (List Number)))
(= (empty-numbers) ())

(: preserve-open-tail
   (-[det]-> Number (List Number) (List Number)))
(= (preserve-open-tail $head $tail) (cons $head $tail))

(: reject-nonlist-tail (-[det]-> Number (List Number)))
(= (reject-nonlist-tail $tail) (cons 1 $tail))
