; A lambda that a call computes is applied as a lambda written in place is:
; its arguments are passed as written, and its body demands them.  So an
; argument the body never uses is never run, and a fixpoint that passes its
; own unfolding as an argument unfolds only as far as it is used.
(= (loop) (loop))
(= (constant) (lam x done))
!((constant) (loop))
!((lam x done) (loop))
!((constant) (println! never-runs))

(= (fix $f) ($f (fix $f)))
(= (P $x $y) (if (== ($x $y) (a b)) True (if (== ($x $y) (b c)) True False)))
(= (∧ $p $q) (if $p $q False))
(= (∨ $p $q) (if $p True $q))
; A path a to c through b, found by the fixpoint of one step.
!((fix (lam self (lam x (lam z (∨ (P x z) (∧ (P x b) (self b z))))))) a c)
!((fix (lam self (lam x (lam z (∨ (P x z) (∧ (P x b) (self b z))))))) c a)

; A used argument is computed where the body uses it.
(= (twice-of) (lam x (pair x x)))
!((twice-of) (+ 1 2))
