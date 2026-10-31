; What explicit evaluation does with a residual: an experimental switch.
;
; A residual is a value returned in expression form, such as a call no
; equation answers.  `--prime-eval-residuals <mode>`, or
; `(pragma! prime-eval-residuals <mode>)`, selects one of three readings:
;
;   answered   the default: `eval` answers a residual as it stands, and the
;              answer keeps its expression form;
;   completed  a call no equation answers is a completed value: a binding
;              reads it as data, and explicit evaluation runs its code;
;   carried    `chain` binds a closed residual as a pending computation, as
;              `let` binds one: it is computed once, when first demanded.
;
; Run this same source under each mode.  Neither alternative is the
; selected semantics.

(: foo (-> Number Number))
(= (late-let) (let $r (foo 5) (let $u (add-atom &self (= (foo 5) 50)) $r)))
(= (late-chain) (chain (foo 6) $r (chain (add-atom &self (= (foo 6) 60)) $u $r)))
(= (shared) (chain (foo 7) $r (let $x $r (let $u (add-atom &self (= (foo 7) 70)) (pair $x $r)))))
(= (late-eval) (let $v (eval (foo 11)) (let $u (add-atom &self (= (foo 11) 110)) $v)))
(= (late-let-run) (let $r (foo 12) (let $u (add-atom &self (= (foo 12) 120)) $r)))

; Every mode: eval of a residual answers it, and terminates.
!(eval (foo 1))
!(eval (quote (foo 1)))
!(chain (foo 1) $v (eval $v))

; Every mode: an open residual carries neither mark nor cell; it is answered
; as it stands.
!(eval (cons-atom foo ($x)))

; By mode: a residual read after an equation for it arrived.  It is
; evaluated again when answered or carried, and read as data when completed.
!(late-let)
!(late-chain)
!(late-eval)

; By mode: a residual bound by `chain` and read twice, with an equation for
; it arriving between the reads.  Carried, it is computed once.
!(shared)

; Every mode: explicit evaluation runs whatever the call returned, a
; completed residual's code included.
!(eval (late-let-run))

; Every mode: a value that holds code runs as that code.
!(eval (cons-atom + (1 2)))
