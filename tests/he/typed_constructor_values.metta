; An application of an HE constructor with one declared function type over
; values is a value where HE's typing finds it well typed, and HE's
; argument error otherwise: in a join's template, a match's template, a
; typed call's argument and at top level.
(: R Type)
(: r (-> Number Number R))
(edge 1 2)
(edge 2 3)
(edge 3 1)
!(collapse (match &self (, (edge $x $y) (edge $y $z)) (r $x $z)))
!(collapse (match &self (edge $x $y) (r $x "s")))
(: k (-> R Number))
(= (k (r $a $b)) (+ $a $b))
!(k (r 1 2))
!(collapse (k (r 1 "z")))
!(r 1 2)
!(collapse (r "a" 2))
!(get-type (r 1 2))
