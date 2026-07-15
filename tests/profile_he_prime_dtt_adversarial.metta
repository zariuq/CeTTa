; Adversarial negatives for the he-prime typing discipline — the semantic
; counterexamples the positive suite cannot see.  Each assert pins the SOUND
; behavior; a regression here means a discipline break, not a style change.

; (1) Nested laundering block: a dynamic (?) child taints the composite edge,
;     so a gradual witness is NOT a proof-grade inhabitant — at any depth,
;     for products and for arrows.
(: dynamic-witness (Wrap %Undefined%))
!(assertEqual (type-inhabit-first &self (Wrap Nat) 0 10000)
              (he-reject (no-inhabitant-at-depth)))
!(assertEqual (type-consistency-kind (Wrap %Undefined%) (Wrap Nat))
              (consistency-kind dynamic))
!(assertEqual (type-consistency-kind (Outer (Wrap %Undefined%)) (Outer (Wrap Nat)))
              (consistency-kind dynamic))
!(assertEqual (type-consistency-kind (-> Nat %Undefined%) (-> Nat Bool))
              (consistency-kind dynamic))
; positive control: an exact witness IS proof-grade
(: nat-witness (Wrap Nat))
!(assertEqual (type-inhabit-first &self (Wrap Nat) 0 10000)
              (he-accept (: nat-witness (Wrap Nat))))
; top licensing stays visible and is not proof-grade either
!(assertEqual (type-consistency-kind (Wrap Nat) (Wrap Atom))
              (consistency-kind top))
!(assertEqual (type-inhabit-first &self (Wrap Atom) 0 10000)
              (he-reject (no-inhabitant-at-depth)))
; gradual checking still accepts the dynamic edge (success typing is not
; proof search): check-typing reports it, named honestly
!(assertEqual (check-typing &self dynamic-witness (Wrap Nat) 10000)
              (he-accept (dynamic (Wrap Nat))))

; (2) Direct grounded-effect exclusion: an unmarked effectful grounded op is
;     refused inside a type, and this rejected call leaves the space untouched.
!(assertEqual (normalize-typing &self (Box (add-atom &self TypeSideEffect)) 10000)
              (he-reject (type-computation-inadmissible)))
!(assertEqual (collapse (match &self TypeSideEffect mutated)) ())
; positive control: type-pure arithmetic still normalizes
!(assertEqual (normalize-typing &self (VecN String (+ 1 1)) 10000)
              (he-accept (VecN String 2)))

; (3) Honest forward-closure accounting: a fuel-starved round is
;     resource-incomplete, never a claimed fixpoint.
(: base (P Z))
(: Mk (-> (P $n) (P (S $n))))
(chaining-rule Mk)
!(assertEqual (type-forward-closure &self 3 1 10)
              (he-accept (forward-closure 1 0 resource-incomplete)))
