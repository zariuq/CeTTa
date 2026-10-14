; The legacy release checks a declaration at its call sites, without
; rejecting an unused definition through whole-program analysis.
(: dormant (-> Number Number))
(= (dormant $value) unexpected)
(= (choices) 1)
(= (choices) 2)
!(test (collapse (choices)) (1 2))
(: select (-> Atom Number))
(= (select a) 3)
(= (select b) 4)
!(test (collapse (select $key)) (3 4))
!(test (collapse (select c)) ())
!(test (collapse (superpose (7 7))) (7 7))
