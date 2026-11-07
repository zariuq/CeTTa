; A type row on the expression itself comes first among its typings: HE binds
; the formal there, then checks the expression's own application at it.
(: List (-> Type Type))
(: Nil (List $a))
(: Cons (-> $a (List $a) (List $a)))
(: (Cons 1 Nil) (List String))
(: k (-> (List String) Number))
(= (k $l) 7)
!(k (Cons 1 Nil))
