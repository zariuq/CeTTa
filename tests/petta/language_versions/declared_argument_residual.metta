; A variable whose declared type conflicts with the callee's parameter is a
; runtime obligation: the default mode keeps it, the strict modes reject it.
(: g (-[det]-> Number Number))
(= (g $n) $n)
(: h (-[det]-> String Number))
(= (h $x) (g $x))
!(test (g 4) 4)
