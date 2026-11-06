; Control for a typed fast-path rule: answered as hyperon-experimental does.
(: List (-> Type Type))
(: Nil (List $a))
(: Cons (-> $a (List $a) (List $a)))
(: f (-> (List String) %Undefined%))
(= (f $l) (Cons "s" (Cons 1 $l)))
!(f Nil)
