; Equation rewriting — various patterns

; Simple ground equation
(= (f a) b)
!(assertEqual (f a) b)

; Variable binding in equation (avoid stdlib name 'id')
(= (my-id $x) $x)
!(assertEqual (my-id hello) hello)
!(assertEqual (my-id 42) 42)

; Multiple variable bindings
(= (swap $x $y) (pair $y $x))
!(assertEqual (swap a b) (pair b a))

; Nested expression in equation
(= (wrap $x) (box (inner $x)))
!(assertEqual (wrap thing) (box (inner thing)))

; Nondeterministic — multiple matching equations
(= (choice) alpha)
(= (choice) beta)
(= (choice) gamma)
!(assertEqualToResult (choice) (alpha beta gamma))

; No matching equation — returned unchanged
!(assertEqualToResult (unknown-func z) ((unknown-func z)))

; Recursive equation (factorial-like, manual unrolling)
(= (fac 0) 1)
(= (fac 1) 1)
!(assertEqual (fac 0) 1)
!(assertEqual (fac 1) 1)
