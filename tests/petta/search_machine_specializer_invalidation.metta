(= (inc $x) (+ $x 1))

(= (apply-bag $f) ($f 10))
(= (apply-bag $f) ($f 20))

!(sort-atom (collapse (apply-bag inc)))

; Removing a source equation must invalidate its derived specialized equation.
!(remove-atom &self (= (apply-bag $f) ($f 10)))
!(apply-bag inc)

; Re-adding the source must rebuild the specialization.
!(add-atom &self (= (apply-bag $f) ($f 10)))
!(sort-atom (collapse (apply-bag inc)))
