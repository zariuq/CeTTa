; Control for a typed fast-path rule: answered as hyperon-experimental does.
(: List (-> Type Type))
(: Nil (List $a))
(: Cons (-> $a (List $a) (List $a)))
(: x (List %Undefined%))
(= (u $v) (Cons "s" $v))
!(u x)
