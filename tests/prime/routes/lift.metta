; Lifted syntax operations on code values.  A quotation is sealed, so code is
; built from code only by `lift`, which names its operation, acts on code
; values, seals what it builds and runs nothing.
;   (lift app @F @A ...)  forms the application code @(F A ...).
;   (lift let K @A @C)    puts the code A in the slot K of the code C, by
;                         lambda's own capture-avoiding substitution.
; The bracket C[K := A] is a second spelling of `lift let`
; (tests/prime/lift_brackets.metta).  Code arguments are evaluated; the slot
; is a binder key and is not.

; ---- (lift M) is the code of M's value: M is evaluated, and each value is
; sealed, as lift makes a name in the sealed calculus
; (Mettapedia.TypeTheory.Calculi.SealedCode.Term.lift).  It is how a value
; bound to a variable enters code, since a quotation of the variable is the
; variable itself.
!(lift (+ 1 2))
!(let $x 5 (Pair (lift $x) @$x))
!(lift @a)
!(collapse (lift (superpose (1 2))))

; ---- Application formation builds syntax; nothing runs.
!(lift app @foo @a)
!(lift app @foo @(println! "must-not-run"))
!(lift app @f @a @b)
; Forming an application is not beta: the redex is kept until the code runs.
!(lift app @(lam x x) @a)
!(let $c (lift app @(lam x x) @a) *$c)

; ---- Substitution into code.  A slot is a symbol, a variable, or a
; structured name @K, whose slots are its drops *@K (@K itself is literal).
!(lift let x @a @(foo x))
!(lift let $x @a @(foo $x))
!(lift let @(var z) @a @(foo *@(var z) @(var z)))
; The code is inserted as written, not evaluated.
!(lift let x @(+ 1 2) @(foo x))
; A nested quotation is sealed.
!(lift let x @a @(foo x @x))
; A slot that does not occur leaves the code unchanged.
!(lift let y @a @(foo x))
; Composition: C[x := (g y)][y := b] equals C[x := (g b), y := b].
!(lift let y @b (lift let x @(g y) @(f x y)))
!(lift let x @(g b) (lift let y @b @(f x y)))
; Sequential and simultaneous substitution differ: [x := y, y := a] on
; (pair x y).  A tuple of slots, as in let, fills them at once.
!(lift let y @a (lift let x @y @(pair x y)))
!(lift let (x y) (@y @a) @(pair x y))
; The code is data until activated, and runs when activated.
!(let $c (lift let x @(+ 1 2) @(* x 10)) (Pair $c *$c))
; Code held by a variable, or computed by a call, takes part.
(= (template) @(greet x))
!(let $who @zar (lift let x $who (template)))

; ---- Hygiene: lambda's binders.
; An inner binder of the slot's key shadows it.
!(lift let y @w @(lam y (pair y z)))
!(lift let x @a @(lam x (foo x)))
; A binder that would capture a name of the inserted code is renamed, so the
; result is alpha-equivalent to (lam z (foo y z)).
!(lift let x @y @(lam y (foo x y)))
; Native beta renames the same binder.
!((lam x (lam y (pair x y))) y)

; ---- Only code values are operated on; anything else stays as written.
!(lift app @foo a)
!(lift let x a @(foo x))
!(lift let 5 @a @(foo x))
!(lift swap @a @b)
!(lift let x @a)
