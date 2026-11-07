; As fold_inversion_sum, for a product.
(: Foo Type)
(: Bar Type)
(: Nil Bar)
(: Cons (-> Number Bar Foo))
(: g (-> Foo Number))
(= (g Nil) 1)
(= (g (Cons $x $r)) (* $x (g $r)))
!(g (Cons 5 Nil))
