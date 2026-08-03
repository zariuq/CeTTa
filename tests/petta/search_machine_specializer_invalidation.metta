(= (inc $x) (+ $x 1))

(= (apply-bag $f) ($f 10))
(= (apply-bag $f) ($f 20))

!(collapse (apply-bag inc))

; Removing a source clause must invalidate its derived specialized clause.
!(remove-atom &self (= (apply-bag $f) ($f 10)))
!(apply-bag inc)

; Re-adding the source must rebuild the specialization in declaration order.
!(add-atom &self (= (apply-bag $f) ($f 10)))
!(collapse (apply-bag inc))
