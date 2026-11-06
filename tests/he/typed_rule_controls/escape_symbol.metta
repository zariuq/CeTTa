; Control for a typed fast-path rule: answered as hyperon-experimental does.
(: List (-> Type Type))
(: Nil (List $a))
(: Cons (-> $a (List $a) (List $a)))
(: g (-> (List String) Number))
(= (g $l) 1)
(= (u $x) (g $x))
(= (call $f $b) (if $b (u (Cons "a" Nil)) ($f (Cons 1 Nil))))
!(call u False)
