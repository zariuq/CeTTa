; Decoding proposition codes in a native proof package.  The family
; Holds : prop -> Sort 0 decodes all@A P to Pi A (Holds (P #0)).  That rule
; keeps the type Sort 0 of its left side only when the carrier A is itself in
; Sort 0; the package is sound for such carriers (valid_all, CodesRead: the
; model reads each quantifier carrier at the bottom level).  For a universe
; the right side would be a type in Sort 1, so no decoding rule is made: the
; quantifier over it is introduced and eliminated by rule constants of the
; signature instead, which have no equation.

!(import! &self pf)
!(bind! &thy (new-space))
!(pf:theory &thy ((: Q (-> (u 0) prop))))
(= (verdict-of $r)
   (unify $r (Established $e) Established
     (unify $r (Undetermined $w) Undetermined
       (unify $r (Refuted $w) Refuted other))))

; A theorem quantified over prop, a carrier in Sort 0, and one quantified
; over the universe (u 0).
!(pf:theorem &thy over-prop (all prop (lam p (imp p p))) (pf:fix p (pf:assume h h)))
!(pf:theorem &thy over-universe (all (u 0) (lam X (imp (Q X) (Q X))))
   (pf:fix X (pf:assume h h)))

; 1. The package over prop is checked by the kernel.
!(let $r (try (set:native-proof &thy over-prop)) (verdict-of $r))
; 2. The package over the universe is checked too, with the quantifier
;    introduced by its rule constant and no rule that changes the level of a
;    type.
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      (set:native-proof &thy over-universe)
   (let (App (App (DeclConst $intro) $predicate) $abstraction) $term
     (OverUniverse $intro)))
!(let $r (try (set:native-proof &thy over-universe)) (verdict-of $r))
