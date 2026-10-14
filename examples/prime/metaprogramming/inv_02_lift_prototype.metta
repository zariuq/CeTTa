; Lifted syntax operations on code values, natively.
;
; This file first held an equation-level prototype of `lift`, ordinary
; equations over `Data` that were first-order and not capture-avoiding.  The
; queries below are the prototype's, now answered by the native operations.
;   (lift app @F @A ...)  forms the application code @(F A ...).
;   (lift let k @A @C)    substitutes the code A for the slot k in the code C.
; Both act on code values and stay as written otherwise.  Each acts on one
; level of code (a nested quotation is sealed) and nothing runs.  `lift let`
; is lambda's own substitution, so it knows binders: the prototype's negative
; hygiene controls at the end now come out hygienic.  A bracket of bindings
; touching code, C[k := A], is a meta-argument the core reads as `lift let`
; (tests/prime/lift_brackets.metta).

(= (observe $v) $v)

; --- application formation ---------------------------------------------------
; 1-2. Builds syntax; the operand's effect does not run.
!(observe (lift app @foo @a))
!(observe (lift app @foo @(println! "must-not-run")))
; 3-4. Forming an application is not beta: the redex is kept, and only its
;      activation reduces it.
!(observe (lift app @(lam x x) @a))
!(let $c (lift app @(lam x x) @a) *$c)
; 5. Argument discipline: an operand that is not a code value stays as written.
!(lift app @foo a)

; --- substitution into code --------------------------------------------------
; 6-7. A symbol slot and a variable slot.
!(observe (lift let x @a @(foo x)))
!(observe (lift let $x @a @(foo $x)))
; 8. The inserted code is inserted as written, not evaluated.
!(observe (lift let x @(+ 1 2) @(foo x)))
; 9. One level: a nested quotation is sealed.
!(observe (lift let x @a @(foo x @x)))
; 10. A slot that does not occur leaves the code unchanged.
!(observe (lift let y @a @(foo x)))
; 11. Argument discipline: an unquoted replacement stays as written.
!(lift let x a @(foo x))
; 12-13. Composition: C[x := (g y)][y := b] equals C[(x := (g b)) (y := b)].
!(observe (lift let y @b (lift let x @(g y) @(f x y))))
!(observe (lift let x @(g b) (lift let y @b @(f x y))))
; 14-15. Sequential and simultaneous differ on (pair x y): [x := y][y := a]
;        against [(x := y) (y := a)].
;        A tuple of slots, as in let, fills them at once.
!(observe (lift let y @a (lift let x @y @(pair x y))))
!(observe (lift let (x y) (@y @a) @(pair x y)))
; 16. The code is data until activated, and runs when activated.
!(let $c (lift let x @(+ 1 2) @(* x 10)) (pair $c *$c))

; --- binders -----------------------------------------------------------------
; 17. Hygiene control: the bound y is not the slot y, so the code is unchanged
;     (the first-order prototype wrongly substituted it).
!(observe (lift let y @w @(lam y (pair y z))))
; 17b. Hygiene control, capture: inserting y under a binder y renames the
;      binder, so the result is alpha-equivalent to (lam z (foo y z)) (the
;      prototype captured it, giving (lam y (foo y y))).
!(observe (lift let x @y @(lam y (foo x y))))
; 17c. Hygiene control, shadowing: the inner binder x shadows the slot, so
;      (lam x (foo x)) is unchanged (the prototype gave (lam a (foo a))).
!(observe (lift let x @a @(lam x (foo x))))
; 18. Native lambda substitution renames the same binder.
!(observe ((lam x (lam y (pair x y))) y))

; --- the bracket spelling ----------------------------------------------------
; 19. A bracket touching a term is a meta-argument, (meta C [k := A]),
;     printed touching; on code the core reads it as lift let.
!(parse "(f @(foo $x)[$x := @a])")
; 20. A bracketed template is one form.
!(parse "@(foo $x)[$x := @a]")
