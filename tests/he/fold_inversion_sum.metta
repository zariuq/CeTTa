; A typed head folds only where its constructor types the recursive field
; at the head's formal: here HE checks (g Nil) at Foo and rejects it.
(: Foo Type)
(: Bar Type)
(: Nil Bar)
(: Cons (-> Number Bar Foo))
(: g (-> Foo Number))
(= (g Nil) 0)
(= (g (Cons $x $r)) (+ $x (g $r)))
!(g (Cons 1 Nil))
!(+ 0 (g (Cons 1 Nil)))
