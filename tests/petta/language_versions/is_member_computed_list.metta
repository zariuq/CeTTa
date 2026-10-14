; A call inside the list is evaluated: (2 (two)) is the list (2 2).
(: two (-[det]-> Number))
(= (two) 2)
(: has-two? (-[det]-> Number Bool))
(= (has-two? $x) (is-member $x (2 (two))))
