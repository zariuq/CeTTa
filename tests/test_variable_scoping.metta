; Variable scoping — variables in different equations are independent
; Ref: minimal-metta.md lines 180-207

; Two equations with same variable name $x — independent scopes
(= (first $x) (pair first $x))
(= (second $x) (pair second $x))
!(assertEqual (first a) (pair first a))
!(assertEqual (second b) (pair second b))

; Variable from equation doesn't leak to caller
(= (get-val) (match &self (= (get-val) $v) $v))
; get-val is recursive but $v in the equation is scoped to the equation
