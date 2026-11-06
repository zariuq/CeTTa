; Control for a typed fast-path rule: answered as hyperon-experimental does.
(: List (-> Type Type))
(: y (List Atom))
(: g (-> (List String) Number))
(= (g $l) 1)
(= (u $v) (g $v))
!(u y)
