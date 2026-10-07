(set:profile hol)

; The equality axioms pf:equality publishes for a carrier bind a predicate
; and two elements.  Their binders are chosen apart from the carrier, so a
; carrier named like a binder is not captured: for a type named P the
; predicate is bound as P1, and for a type named a the element as a1.
!(import! &self pf)
!(set:inductive &self P (u 0) (: pz P) (: ps (-> P P)))
!(pf:equality &self P refl@P subst@P)
!(set:known-proposition &self subst@P)
!(set:known-proposition &self refl@P)
; Substitution at P proves symmetry at P.
!(pf:theorem &self sym-P (all P (lam x (all P (lam y (imp (eq P x y) (eq P y x))))))
   (pf:fix x (pf:fix y (pf:assume h (pf:sym P x y h)))))
!(set:inductive &self a (u 0) (: az a))
!(pf:equality &self a refl@a subst@a)
!(set:known-proposition &self refl@a)
!(pf:theorem &self refl-a (all a (lam x (eq a x x))) (pf:fix x (pf:refl a x)))
; A carrier that mentions no binder keeps the usual names.
!(set:inductive &self num (u 0) (: zero num) (: suc (-> num num)))
!(pf:equality &self num refl@num subst@num)
!(set:known-proposition &self subst@num)
; Negative control: a statement that is false at P is not proved by the
; axioms.
!(pf:theorem &self wrong-P (all P (lam x (eq P x pz))) (pf:fix x (pf:refl P x)))
