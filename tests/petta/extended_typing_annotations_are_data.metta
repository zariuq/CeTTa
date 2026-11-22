; Outside the typecheck profiles, `brand`, `the` and the determinism-annotated
; arrows are ordinary data: extended reads them as the default profile does.
!(brand T 5)
!(the T 5)
!(the Number (+ 2 3))
(: f (-[det]-> A B))
!(match &self (: f $type) $type)
(= (marked $x) (brand Marker $x))
!(marked 7)

; Like any other head, `the` takes the program's own equations.
(= (the $type $value) (typed $value))
!(the T 8)
