; An authored lambda is a value on every evaluation route: nothing under its
; binder runs until an application supplies the argument, whether the lambda
; is written in place, passed through an equation, bound by let, or stored
; in a constructor.  A value the lambda captured from outside is read back
; as that value.  This file is run with and without --fuel; the two answers
; must be the same.
(= (∧ $p $q) (if $p $q False))
(= (id1 $x) $x)
!(lam z (∧ (F z) (G z)))
!(id1 (lam z (∧ (F z) (G z))))
!(id1 (lam z (+ 1 2)))
!(id1 (box (lam z (+ 1 2))))
!(let $f (lam z (+ 1 2)) (id1 $f))
!(id1 (lam (z (w : Number)) (∧ z w)))
!(id1 (lam z @(∧ z z)))
; A captured value is the value of the outside expression.
!(let $y (+ 1 2) (lam z $y))
!(let $y (+ 1 2) (id1 (lam z (+ z $y))))
!(let $y (superpose (1 2)) (id1 (lam z (+ z $y))))
; Applied, the body runs.
!((id1 (lam z (+ z 1))) 2)
!((id1 (lam z (∧ z True))) False)
; Sealed code is never rewritten inside.
!(id1 @(∧ (F z) (G z)))

; A computed head is selected once with its bindings and shared choices.
; Reusing that head keeps the selected function; separate head occurrences
; choose independently. A lambda body runs at application, and a quotation
; or unknown head remains data. Ignored and pruned computations stay silent.
(= (head-apply $f) ($f 3))
(= (head-apply-twice $f) (Pair ($f 1) ($f 2)))
(= (head-ignore $f) ignored)
(= (head-omega) (head-omega))
(= (head-choice) (lam x (+ x 1)))
(= (head-choice) (lam x (+ x 2)))
(= (head-choice) (lam x (+ x 1)))
(= (head-paid) (chain (println! head-production) $_ (head-choice)))
!(head-apply-twice (head-choice))
!(Pair (head-apply (head-choice)) (head-apply (head-choice)))
!(head-apply-twice (head-paid))
!(head-ignore (head-omega))
!(head-ignore (chain (println! forbidden-head) $_ (lam x x)))
!(head-ignore (lam x (println! forbidden-body)))
!(head-apply-twice (chain (println! lambda-production) $_ (lam x (chain (println! lambda-application) $_ x))))
!(head-apply (superpose ((quote (+ 1 2)) (lam x (+ x 1)) inert (lam x (+ x 1)))))
!(head-apply (let $a 9 (lam x (+ x $a))))
!(head-apply (head-ignore (head-omega)))
!(once (head-apply (superpose ((lam x (+ x 1)) (lam x (chain (println! forbidden-pruned) $_ x))))))
!(head-apply (Pair (lam x (println! forbidden-field-body)) held))
!(head-apply-twice (superpose ((lam x (Pair x x)) inert)))
