; Control for a typed fast-path rule: answered as hyperon-experimental does.
(: List (-> Type Type))
(: x (List %Undefined%))
(: g (-> (List String) Number))
(= (g $l) 1)
(= (pick 0) x)
(= (pick 1) x)
!(g (pick 0))
