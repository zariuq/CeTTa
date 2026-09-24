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

; The universal-elimination compiler supplies a concrete checked witness.
!(set:theorem native-power-empty
   (In Empty (Power Empty))
   (pf:all-elim (pf:known PowerI) Empty))
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      (set:native-proof native-power-empty)
      (NativeProof $name (size-atom $assumptions)))

; Unsupported convenience proof syntax is an abstention, not evidence.
!(set:theorem native-by
   (In Empty (Power Empty))
   (pf:by PowerI))
!(let $result (try (set:native-proof native-by)) (car-atom $result))

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
; PowerI remains an explicit premise of the compiled package, not a new
; closed truth established by executing its consumer.
!(set:theorem native-universal-power
   (all set (lam x (In x (Power x))))
   (pf:all-intro (pf:all-elim (pf:known PowerI) (pf:var 0))))
!(let $package (set:native-proof native-universal-power)
   (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
        $package
     (let (App (DeclConst $family) $formula) $type
       (let (SetNativeUseV1 $checked $consumer $result $result-type)
            (set:native-use $package (Lam $type (App (idx 0) (DeclConst Empty))))
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
     (let (App (DeclConst $family) $formula) $type
       (let (SetNativeUseV1 $checked $consumer $result $result-type)
            (set:native-use $package
              (Lam $type (App (idx 0) (App (DeclConst Power) (DeclConst Empty)))))
         (NativeUniversalUse
           (== $checked $package)
           (== $result-type
             (App (DeclConst $family)
               (App (App (DeclConst In) (App (DeclConst Power) (DeclConst Empty)))
                 (App (DeclConst Power) (App (DeclConst Power) (DeclConst Empty))))))
           (size-atom $assumptions))))))
; A universe is not an object of the declared SET carrier.
!(let $package (set:native-proof native-universal-power)
   (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
        $package
     (let $result (try (set:native-use $package (Lam $type (App (idx 0) U0))))
       (car-atom $result))))
