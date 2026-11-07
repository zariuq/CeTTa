; A type row on an expression below the value's top: HE binds the formal from
; the first typing and then checks that node's own application there.
(: List (-> Type Type))
(: Nil (List $a))
(: Cons (-> $a (List $a) (List $a)))
(: W Type)
(: Foo Type)
(: Wrap (-> Number W))
(: (Wrap 1) Foo)
(: g (-> (List $t) Number))
(= (g $l) 7)
!(g (Cons (Wrap 1) Nil))
