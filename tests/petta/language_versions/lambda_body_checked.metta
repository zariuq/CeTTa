; A lambda's body is checked against the arrow it is passed as: its second
; branch returns an arbitrary atom where a Number is promised.
(: apply-to (-[nondet]-> (-[nondet]-> Atom Number) Atom Number))
(= (apply-to $function $value) ($function $value))
(: through-lambda (-[nondet]-> Atom Number))
(= (through-lambda $value)
   (apply-to (|-> ($x) (if (is-var $x) (the Number $x) $x)) $value))
