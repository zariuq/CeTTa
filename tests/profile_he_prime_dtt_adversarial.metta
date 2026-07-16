; Adversarial negatives for the he-prime typing discipline — the semantic
; counterexamples the positive suite cannot see.  Each assert pins the SOUND
; behavior; a regression here means a discipline break, not a style change.

; (1) Nested laundering block: a dynamic (?) child taints the composite edge,
;     so a gradual witness is NOT an exact-or-structural inhabitant — at any depth,
;     for products and for arrows.
(: dynamic-witness (Wrap %Undefined%))
!(assertEqual (search-first-inhabitant &self (Wrap Nat) 0 10000)
              (he-reject (no-inhabitant-at-depth)))
!(assertEqual (is-consistent (Wrap %Undefined%) (Wrap Nat))
              (he-accept dynamic))
!(assertEqual (is-consistent (Outer (Wrap %Undefined%)) (Outer (Wrap Nat)))
              (he-accept dynamic))
!(assertEqual (is-consistent (-> Nat %Undefined%) (-> Nat Bool))
              (he-accept dynamic))
; positive control: an exact witness IS exact-or-structural
(: nat-witness (Wrap Nat))
!(assertEqual (search-first-inhabitant &self (Wrap Nat) 0 10000)
              (he-accept (: nat-witness (Wrap Nat))))
; the top tag stays visible and is not exact-or-structural either
!(assertEqual (is-consistent (Wrap Nat) (Wrap Atom))
              (he-accept top))
!(assertEqual (search-first-inhabitant &self (Wrap Atom) 0 10000)
              (he-reject (no-inhabitant-at-depth)))
; gradual checking still accepts the dynamic edge (success typing is not
; proof search): check-type reports it, named honestly
!(assertEqual (check-type &self dynamic-witness (Wrap Nat) 10000)
              (he-accept (dynamic (Wrap Nat))))

; (2) Direct grounded-effect exclusion: an unmarked effectful grounded op is
;     refused inside a type, and this rejected call leaves the space untouched.
!(assertEqual (normalize-type &self (Box (add-atom &self TypeSideEffect)) 10000)
              (he-reject (type-computation-inadmissible)))
!(assertEqual (collapse (match &self TypeSideEffect mutated)) ())
; positive control: type-pure arithmetic still normalizes
!(assertEqual (normalize-type &self (VecN String (+ 1 1)) 10000)
              (he-accept (VecN String 2)))

; (3) Honest forward-closure accounting: a fuel-starved round is
;     resource-incomplete, never a claimed fixpoint.
!(assertEqual (type-forward-closure &self 1 10000 10)
              (he-accept (forward-closure 1 0 fixpoint)))
(: base (P Z))
(: Mk (-> (P $n) (P (S $n))))
(chaining-rule Mk)
!(assertEqual (type-forward-closure &self 3 1 10)
              (he-accept (forward-closure 1 0 resource-incomplete)))
