; Outside the licence: mylist is no function and offers (List Number), so
; a value typed at the list sort need not be a spine.  The fold keeps its
; cell test; (sum mylist) answers nothing, as its equations do.
(: List (-> Type Type))
(: Nil (List $a))
(: Cons (-> $a (List $a) (List $a)))
(: build (-> Number (List Number) (List Number)))
(= (build $n $acc) (if (== $n 0) $acc (build (- $n 1) (Cons $n $acc))))
(: sum (-> (List Number) Number))
(= (sum Nil) 0)
(= (sum (Cons $x $xs)) (+ $x (sum $xs)))
(: mylist (List Number))
!(sum (build 100 Nil))
!(sum mylist)
