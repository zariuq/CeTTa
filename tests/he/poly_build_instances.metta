; A polymorphic build whose accumulator is passed at another instance: the
; cell it builds is checked, as HE checks it, while a well-typed call answers.
; The error names the expected instance, as typed_open_structural records it
; (hyperon-experimental prints the formal's variable there).
(: List (-> Type Type))
(: Nil (List $a))
(: Cons (-> $a (List $a) (List $a)))
(: build (-> Number (List $a) (List $a)))
(= (build $n $acc) (if (== $n 0) $acc (build (- $n 1) (Cons $n $acc))))
(: sum (-> (List $a) Number))
(= (sum Nil) 0)
(= (sum (Cons $x $xs)) (+ $x (sum $xs)))
!(sum (build 4 Nil))
!(build 0 (Cons "s" Nil))
!(build 2 (Cons "s" Nil))
