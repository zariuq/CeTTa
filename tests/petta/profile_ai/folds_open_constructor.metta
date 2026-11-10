; Outside the licence: Snoc is a second constructor of the list sort.  The
; fold keeps its cell test; (sum (Snoc 5 Nil)) answers nothing, as its
; equations do.
(: List (-> Type Type))
(: Nil (List $a))
(: Cons (-> $a (List $a) (List $a)))
(: Snoc (-> $a (List $a) (List $a)))
(: build (-> Number (List Number) (List Number)))
(= (build $n $acc) (if (== $n 0) $acc (build (- $n 1) (Cons $n $acc))))
(: sum (-> (List Number) Number))
(= (sum Nil) 0)
(= (sum (Cons $x $xs)) (+ $x (sum $xs)))
!(sum (build 100 Nil))
!(sum (Snoc 5 Nil))
