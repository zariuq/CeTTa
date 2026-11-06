; Control for a typed fast-path rule: answered as hyperon-experimental does.
(: List (-> Type Type))
(: Nil (List $a))
(: Cons (-> $a (List $a) (List $a)))
(: g (-> (List String) %Undefined%))
(= (g $l) (u $l))
(= (u $l) (Cons "s" (Cons 1 $l)))
!(g Nil)
