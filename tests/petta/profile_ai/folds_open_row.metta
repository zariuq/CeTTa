; Outside the licence: Cons is declared at a closed row, not the sort's
; usual one, so the fold keeps its cell test.  The answers are the same.
(: List (-> Type Type))
(: Nil (List $a))
(: Cons (-> Number (List Number) (List Number)))
(: build (-> Number (List Number) (List Number)))
(= (build $n $acc) (if (== $n 0) $acc (build (- $n 1) (Cons $n $acc))))
(: sum (-> (List Number) Number))
(= (sum Nil) 0)
(= (sum (Cons $x $xs)) (+ $x (sum $xs)))
!(sum (build 100 Nil))
!(sum (Cons 1 (Cons 2 Nil)))
