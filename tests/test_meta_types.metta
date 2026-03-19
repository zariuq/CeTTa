; Meta-types — Symbol, Variable, Expression, Grounded, Atom, Type
; Ref: metta.md lines 108-123

; Atom type in function signature — arg passed UNEVALUATED (lazy)
(: identity (-> Atom Atom))
(= (identity $x) $x)
; identity preserves unevaluated expression
!(assertEqualToResult (identity (+ 1 2)) ((+ 1 2)))

; Without type annotation, argument IS evaluated
(= (normal-id $x) $x)
!(assertEqual (normal-id (+ 1 2)) 3)
