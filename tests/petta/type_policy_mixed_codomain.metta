(: mixed-body (-> Number Atom))
(: mixed-body (-> Number %Undefined%))
(= (mixed-body $x) (+ $x 1))
!(collapse (mixed-body 6))
