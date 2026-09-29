; SWI-PeTTa's registration gives min and max the arity 2 alone: other
; applications are partial.
!(max 3 4)
!(max 3)
!(min (5 1.0))
; sqrt is registered with no arity: every application is partial.
!(sqrt 4)
!(sqrt 4 5)
; != has the arity 2.
!(!= 1 2)
!(!= 1)
; Registered Prolog library functions at their arities are calls.
!(atom_concat abc def)
!(last (1 2 3))
!(member 2 (1 2 3))
; Names CeTTa owns natively are partial below their registered arity too,
; at the root as where nested.
!(car-atom)
!(pow-math 7)
!(and True)
!(msort)
!(cons 1)
!(let $f (superpose) $f)
; The reference prelude's own definitions.
!(exp 0)
!(implies True False)
!(implies False False)
!(=? (f $x) (f 1))
!(=? a b)
!(= $x 5)
