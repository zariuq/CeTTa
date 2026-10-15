; A lambda whose body gives the arrow's result type passes the check.
(: apply-to (-[nondet]-> (-[nondet]-> Atom Number) Atom Number))
(= (apply-to $function $value) ($function $value))
(: through-lambda (-[nondet]-> Atom Number))
(= (through-lambda $value)
   (apply-to (|-> ($x) (if (is-var $x) 0 1)) $value))

!(test (through-lambda name) 1)
