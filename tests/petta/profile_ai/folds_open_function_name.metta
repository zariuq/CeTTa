; Outside the licence: lst is a function whose declaration is no arrow, so
; its name is data of the list sort.  The fold keeps its cell test;
; (sum lst) answers nothing, as its equations do.
(: List (-> Type Type))
(: Nil (List $a))
(: Cons (-> $a (List $a) (List $a)))
(: build (-> Number (List Number) (List Number)))
(= (build $n $acc) (if (== $n 0) $acc (build (- $n 1) (Cons $n $acc))))
(: sum (-> (List Number) Number))
(= (sum Nil) 0)
(= (sum (Cons $x $xs)) (+ $x (sum $xs)))
(: lst (List Number))
(= (lst) Nil)
!(sum (build 100 Nil))
!(sum lst)
