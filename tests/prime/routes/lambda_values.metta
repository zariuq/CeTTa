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
