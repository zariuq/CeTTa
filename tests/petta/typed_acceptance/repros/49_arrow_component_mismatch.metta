; Negative structural witness: matching modes and arities do not hide an
; incompatible result component in a higher-order parameter.

(: apply-number (-[det]-> (-[det]-> Number Number) Number Number))
(= (apply-number $fn $value) ($fn $value))

(: render-number (-[det]-> Number String))
(= (render-number $value) "rendered")

(: invalid-use (-[det]-> Number Number))
(= (invalid-use $value) (apply-number render-number $value))
