; The legacy release checks a declaration at its call sites, without
; rejecting an unused definition through whole-program analysis.
(: dormant (-> Number Number))
(= (dormant $value) unexpected)
(= (choices) 1)
(= (choices) 2)
!(test (sort-atom (collapse (choices))) (sort-atom (1 2)))
(: select (-> Atom Number))
(= (select a) 3)
(= (select b) 4)
!(test (sort-atom (collapse (select $key))) (sort-atom (3 4)))
!(test (collapse (select c)) ())
!(test (collapse (superpose (7 7))) (7 7))
