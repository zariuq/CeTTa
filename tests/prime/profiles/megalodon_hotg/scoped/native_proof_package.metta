(set:profile megalodon-hotg)

; A checked HOL proof becomes a dependent proof-family term only by compiling
; its retained proof tree and replaying that term in the regular kernel.

; Closed logical proof: no assumptions are introduced.
!(set:theorem native-id
   (all prop (lam p (imp p p)))
   (pf:all-intro (pf:imp-intro (pf:hyp 0))))
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      (set:native-proof native-id)
      (NativeProof $name $term (size-atom $assumptions)))
; The exact checked package can be consumed by an intrinsic dependent
; function.  Here the result type depends on the compiled proof itself:
; applying proof reflexivity yields Id P p p for that exact p.
!(let $package (set:native-proof native-id)
   (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
        $package
     (let (SetNativeUseV1 $checked $consumer $result $result-type)
          (set:native-use $package (Lam $type (Refl (idx 0))))
       (let (Id $carrier $left $right) $result-type
         (NativeUse
           (== $checked $package)
           (== $result (App (Lam $type (Refl (idx 0))) $term))
           (== $left $term)
           (== $right $term))))))

; A consumer with the wrong domain is a refuted submitted application.
!(let $package (set:native-proof native-id)
   (let $result
        (try (set:native-use $package
               (Lam (Sort (LevelConst 0)) (Refl (idx 0)))))
     (car-atom $result)))

; Package-shaped syntax is not authority.  Changing even the compiled term
; makes the package disagree with a fresh compilation of the admitted proof,
; and the use is refuted.
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      (set:native-proof native-id)
   (let $forged
        (SetNativeProofV1 $name $prop $proof (Refl $term) $type
          $context $rules $assumptions $digest)
     (let $result
          (try (set:native-use $forged (Lam $type (Refl (idx 0)))))
       (car-atom $result))))

; A foreign signature digest is also an abstention, even if the proof term
; and every other field were copied from an accepted package.
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      (set:native-proof native-id)
   (let $foreign
        (SetNativeProofV1 $name $prop $proof $term $type
          $context $rules $assumptions "foreign-signature")
     (let $result
          (try (set:native-use $foreign (Lam $type (Refl (idx 0)))))
       (car-atom $result))))
!(let (Established
         (PrimeScopedValue
           (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)))
      (try (set:native-proof native-id))
      (NativeTryProof $name $term (size-atom $assumptions)))

; Object and proof binders share the target de Bruijn context.  The innermost
; proof hypothesis must remain index zero beneath the outer object binder.
; The family decodes a quantifier only over a carrier of the least
; universe.  `set`, the sort of all sets, is above every universe, so a
; quantifier over all sets is introduced and eliminated by the rule
; constants `allI@set` and `allE@set` of the package's signature, and the
; implication inside it still by abstraction.
!(set:theorem native-mixed
   (all set (lam x (imp (In x x) (In x x))))
   (pf:all-intro (pf:imp-intro (pf:hyp 0))))
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      (set:native-proof native-mixed)
      (NativeProof $name $term (size-atom $assumptions)))

; A cited axiom is never laundered into a closed proof.  It remains an exact
; declaration assumption in the package context and provenance list.
!(set:axiom native-premise (In Empty (Power Empty)))
!(set:theorem native-from-premise
   (In Empty (Power Empty))
   (pf:known native-premise))
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      (set:native-proof native-from-premise)
      (NativeProof $name (size-atom $assumptions)))

; Every set is a member of its power set: the law of the power set at the set
; itself, whose second half, read from right to left, applies to the proof
; that every member of the set is a member of it.
!(set:theorem power-self (all set (lam x (In x (Power x))))
   (pf:all-intro
     (pf:imp-elim
       (pf:imp-elim
         (pf:all-elim (pf:all-elim (pf:all-elim (pf:known powerLaw) (pf:var 0)) (pf:var 0))
           (imp (all set (lam y (imp (In y (pf:var 0)) (In y (pf:var 0)))))
                (In (pf:var 0) (Power (pf:var 0)))))
         (pf:imp-intro (pf:imp-intro (pf:hyp 0))))
       (pf:all-intro (pf:imp-intro (pf:hyp 0))))))

; The universal-elimination compiler supplies a concrete checked witness.
!(set:theorem native-power-empty
   (In Empty (Power Empty))
   (pf:all-elim (pf:known power-self) Empty))
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      (set:native-proof native-power-empty)
      (NativeProof $name (size-atom $assumptions)))

; A readable step is evidence only through what it stands for: the package
; of a proof by pf:by is compiled from the eliminations the checker found for
; the step, and carries them as its proof.
!(set:theorem native-by
   (In Empty (Power Empty))
   (pf:by power-self))
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      (set:native-proof native-by)
   (Compiled $proof))

; A saved package cannot be consumed after its recorded premise is
; withdrawn, and the theorem itself no longer compiles either.
!(set:axiom transient-premise (In Empty Empty))
!(set:theorem transient-copy (In Empty Empty) (pf:known transient-premise))
!(let $package (set:native-proof transient-copy)
   (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
        $package
     (let $removed
          (match &self
            (set:known transient-premise $premise $origin $tree $depends $revision $signature)
            (remove-atom &self
              (set:known transient-premise $premise $origin $tree $depends $revision $signature)))
       (let $result
            (try (set:native-use $package (Lam $type (Refl (idx 0)))))
         (car-atom $result)))))
!(let $result (try (set:native-proof transient-copy)) (car-atom $result))

; Missing theorem names also abstain explicitly under `try`.
!(let $result (try (set:native-proof absent-native-proof)) (car-atom $result))

; One retained universal SET theorem serves different native object inputs.
; The law of the power set, through power-self, remains an explicit premise
; of the compiled package, not a new closed truth established by executing
; its consumer.  Its proof type is the
; code of a quantifier over all sets, which the family does not decode: a
; consumer eliminates it with the rule constant `allE@set`, at the predicate
; the type names.
!(set:theorem native-universal-power
   (all set (lam x (In x (Power x))))
   (pf:all-intro (pf:all-elim (pf:known power-self) (pf:var 0))))
!(let $package (set:native-proof native-universal-power)
   (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
        $package
     (let (App (DeclConst $family) (App (DeclConst $all) $predicate)) $type
       (let (SetNativeUseV1 $checked $consumer $result $result-type)
            (set:native-use $package
              (Lam $type (App (App (App (DeclConst allE@set) $predicate) (idx 0))
                              (DeclConst Empty))))
         (NativeUniversalUse
           (== $checked $package)
           (== $result-type
             (App (DeclConst $family)
               (App (App (DeclConst In) (DeclConst Empty))
                 (App (DeclConst Power) (DeclConst Empty)))))
           (size-atom $assumptions))))))
!(let $package (set:native-proof native-universal-power)
   (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
        $package
     (let (App (DeclConst $family) (App (DeclConst $all) $predicate)) $type
       (let (SetNativeUseV1 $checked $consumer $result $result-type)
            (set:native-use $package
              (Lam $type (App (App (App (DeclConst allE@set) $predicate) (idx 0))
                              (App (DeclConst Power) (DeclConst Empty)))))
         (NativeUniversalUse
           (== $checked $package)
           (== $result-type
             (App (DeclConst $family)
               (App (App (DeclConst In) (App (DeclConst Power) (DeclConst Empty)))
                 (App (DeclConst Power) (App (DeclConst Power) (DeclConst Empty))))))
           (size-atom $assumptions))))))
; A universe is a set: the universe U0 is an object of the carrier `set`.
; The sort of all sets is not, and the proof is no function to apply
; without the rule constant.
!(let $package (set:native-proof native-universal-power)
   (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
        $package
     (let (App (DeclConst $family) (App (DeclConst $all) $predicate)) $type
       (let $result (try (set:native-use $package
                      (Lam $type (App (App (App (DeclConst allE@set) $predicate) (idx 0))
                                      U0))))
         (car-atom $result)))))
!(let $package (set:native-proof native-universal-power)
   (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
        $package
     (let (App (DeclConst $family) (App (DeclConst $all) $predicate)) $type
       (let $result (try (set:native-use $package
                      (Lam $type (App (App (App (DeclConst allE@set) $predicate) (idx 0))
                                      (Sort (LevelAbove 0))))))
         (car-atom $result)))))
!(let $package (set:native-proof native-universal-power)
   (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
        $package
     (let $result (try (set:native-use $package (Lam $type (App (idx 0) U0))))
       (car-atom $result))))
