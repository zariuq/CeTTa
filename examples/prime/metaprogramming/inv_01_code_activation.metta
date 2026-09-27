; Code values and their activation.
;
; `@e` is a sealed literal: the code of `e`, never evaluated and never entered
; by substitution.  `*c` activates code: it runs `c`'s payload as a
; computation, by need, once per demanded occurrence, and distributes over a
; choice of code values.  `*` on something that is not code stays as the
; residual `(unquote v)`.  A `Data` parameter holds `*@e` as written, and a
; syntax-consuming judgment such as `type:check` splices it.

(= (observe $v) $v)
(= (foo $x) (computed $x))
(= (two) left)
(= (two) right)

; 1-2. A quotation is a value; activation runs it.
!(observe @(+ 1 2))
!(observe *@(+ 1 2))

; 3-4. Code bound to a variable, and to a lambda parameter, then activated.
!(let $c @(+ 1 2) (pair $c *$c))
!((lam c (pair c *c)) @(+ 1 2))

; 5-6. Activating a call to a user equation, and open code: a free matcher
;      variable in code stays a query variable when the code runs.
!(observe *@(foo (+ 1 2)))
!(observe *@(foo $free))

; 7. Activation of a non-code value is the residual (unquote v).
!(let $n 3 *$n)

; 8. One star removes one level of quotation.
!(observe *@@(+ 1 2))

; 9-10. Branching: activation yields every answer of the code, and a choice
;       of code values activates each branch.
!(observe *@(two))
!(let $c (superpose (@(+ 1 1) @(+ 2 2))) *$c)

; 11-13. Need: nothing runs until demanded; an activation in an unchosen
;        branch never runs; one bound activation used twice runs once.
(= (maybe-run $flag $c) (if $flag *$c skipped))
!(let $c @(println! "must-not-run: built only") done)
!(maybe-run False @(println! "must-not-run: unchosen branch"))
!(let $c @(println! "runs once") (let $r *$c (observe (pair $r $r))))

; 14. Two demanded occurrences are two runs.
!(let $c @(println! "runs per occurrence")
   (let $a *$c (let $b *$c (observe (pair $a $b)))))

; 15. Activation inside quoted code is itself code: nothing runs.
!(observe @(pair *@(println! "must-not-run: inside code") 0))

; 16-17. A Data parameter holds `*@e` as written; explicit eval then runs it.
(: hold (-> (Data Atom) (Data Atom)))
(= (hold $x) $x)
!(hold *@(+ 1 2))
!(let $h (hold *@(+ 1 2)) (eval $h))

; 18-19. A syntax-consuming judgment splices activated code: the spliced
;        lambda is checked, while the bare quotation is no lambda.
!(type:check *@(lam x x) (-> u0 u0))
!(type:check @(lam x x) (-> u0 u0))

; 20-21. The sealed literal under a binder: substitution never enters `@`.
!((lam x @(f x)) payload)
!((lam x (pair x @x)) payload)
