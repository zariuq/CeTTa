; Negative mode witness: a closure that may produce several answers cannot
; satisfy a deterministic higher-order parameter.

(: apply-one (-[det]-> (-[det]-> Number Number) Number Number))
(= (apply-one $fn $value) ($fn $value))

(: choose-neighbor (-[nondet]-> Number Number))
(= (choose-neighbor $value) (superpose ($value (+ $value 1))))

(: invalid-use (-[det]-> Number Number))
(= (invalid-use $value) (apply-one choose-neighbor $value))
