; A type row on the expression itself comes first among its typings: HE binds
; the formal there, then checks the expression's own application at it.
(: List (-> Type Type))
(: Nil (List $a))
(: Cons (-> $a (List $a) (List $a)))
(: Pair (-> Type Type Type))
(: (Cons 1 Nil) (List (Pair Number Number)))
(: g (-> (List $t) Number))
(= (g $l) 7)
(= (k $n) (g (Cons $n Nil)))
!(k 1)
