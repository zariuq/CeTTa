; Control for a typed fast-path rule: answered as hyperon-experimental does.
(: List (-> Type Type))
(: x (List %Undefined%))
(: g (-> (List String) Number))
(= (g $l) 1)
(= (u $v) (g $v))
(= (w) u)
!(u x)
