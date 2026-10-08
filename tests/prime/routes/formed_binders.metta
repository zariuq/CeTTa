; This fixture selects rule-M ownership of generated binder syntax.
(scope:profile mercury-implicit per-call reference)

; A binder formed by substitution is sealed like a written one: a mention,
; in quoted code, of a name its pattern binds is the quotation's own, so the
; pattern's value never enters the quotation.  The binder is formed when a
; substitution puts its head (let, case, ...) at the head of an expression:
; the environment's (a name bound to `let`), lambda's (beta), a match's
; bindings.  A binder built by cons-atom or `lift`, and a lambda formed so
; (formed_lambdas.metta), are formed too.
(op let)

; ---- Every way of forming (let $x 5 (quote $x)) answers as the written one.
!(row written)
!(let $x 5 (quote $x))
!(row environment)
!(let $h let ($h $x 5 (quote $x)))
; The lambda owns $x, and its activation copies it wherever it occurs, the
; quoted mention included (stage 5): the answer is that activation's
; variable.
!(row beta)
!((lam h (h $x 5 (quote $x))) let)
!(row unify-branch)
!(unify (op let) (op $h) ($h $x 5 (quote $x)) none)
!(row case-branch)
!(case (op let) (((op $h) ($h $x 5 (quote $x)))))
!(row cons-atom)
!(cons-atom let ($x 5 (quote $x)))
!(row case-formed)
!(let $h case ($h 5 (($x (quote $x)))))
; The instance `lift let` makes is an opening of the code (stage 5, item 3):
; the $x the written quotation owns is copied for it, so the instance prints
; its copy.
!(row lift-let-in-code)
!(let $c (lift let h @let @(h $x 5 @$x)) (Pair $c *$c))

; ---- Negative controls.  A crossing set on the quotation lets the name in;
; a head that binds nothing seals nothing; a quotation built at run time
; around the name shares it (runtime_quote_shares.metta).
!(row crossing-set)
!(let $h let ($h $x 5 (quote $x){$x}))
!(row not-a-binder)
!(let $h f ($h $x (quote $x)))
!(row quotation-built-in-body)
!(let $h let ($h $x 5 (cons-atom quote ($x))))
