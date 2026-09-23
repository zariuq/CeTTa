; A higher-order pair update composed inside a retained-proof consumer.
; The set-valued pair is computed, while the other component retains an
; identity witness indexed by the exact imported proof.
; Pairs are checked at an explicit Sigma using a typed identity continuation;
; this exercises checking, not reconstruction of a dependent pair type.
!(set:theorem pair-consumer-law (all prop (lam p (imp p p)))
  (pf:all-intro (pf:imp-intro (pf:hyp 0))))

(= (update-first)
   (Lam (Pi (DeclConst set) (DeclConst set))
     (Lam (Sigma (DeclConst set) (DeclConst set))
       (App (Lam (Sigma (DeclConst set) (DeclConst set)) (idx 0))
         (Pair (App (idx 1) (Fst (idx 0))) (Snd (idx 0)))))))

(= (update-pair $function $pair)
   (let $update (update-first) (App (App $update $function) $pair)))

(= (checked-pair-update $theorem $function)
 (let $package (set:native-proof $theorem)
  (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
    (let $pair (Pair (DeclConst Empty) (App (DeclConst Power) (DeclConst Empty)))
      (let $updated (update-pair $function $pair)
        (let $consumer (Lam $type
          (App (Lam (Sigma (Sigma (DeclConst set) (DeclConst set))
                          (Id $type (idx 1) (idx 1))) (idx 0))
               (Pair $updated (Refl (idx 0)))))
          (let (SetNativeNormalFormV1 $checked $body $application $normal $original-type $result-type)
               (set:native-normalize $package $consumer)
            (let (Pair $values $witness) $normal
              (let (Sigma $pair-type (Id $proof-type $left $right)) $result-type
              (NativeHigherOrderPair $name
                (== $checked $package)
                (== $values $pair)
                (== $values (Pair (App (DeclConst Power) (DeclConst Empty)) (DeclConst Empty)))
                (== $witness (Refl $term))
                (== $pair-type (Sigma (DeclConst set) (DeclConst set)))
                (== $left $term) (== $right $term)
                (size-atom $assumptions)))))))))))

; A function over proofs is not a set transformer, even if its erased
; lambda body is the same identity term.
(= (wrong-pair-update $theorem)
 (let $package (set:native-proof $theorem)
  (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
    (let $pair (Pair (DeclConst Empty) (App (DeclConst Power) (DeclConst Empty)))
      (let $updated (update-pair (Lam $type (idx 0)) $pair)
        (let $bad (try (set:native-normalize $package
                      (Lam $type
                        (App (Lam (Sigma (Sigma (DeclConst set) (DeclConst set))
                                        (Id $type (idx 1) (idx 1))) (idx 0))
                             (Pair $updated (Refl (idx 0)))))))
          (car-atom $bad)))))))

!(checked-pair-update pair-consumer-law (Lam (DeclConst set) (idx 0)))
!(wrong-pair-update pair-consumer-law)

; A function over type families returns an identity at F(point). Specializing
; F to the constant set family introduces beta redexes in the result type;
; conversion must handle those before the result can update a set pair.
(= (family-identity $point)
   (Lam (Pi (DeclConst set) (Sort (LevelConst 0)))
     (Lam (App (idx 0) $point) (idx 0))))

!(let $specializer (family-identity (DeclConst Empty))
  (let $specialized (App $specializer (Lam (DeclConst set) (DeclConst set)))
    (let $result (checked-pair-update pair-consumer-law $specialized)
      (TypeFamilySpecialization $result))))

; A function returning an identity proof is not a family of types.
!(let $package (set:native-proof pair-consumer-law)
  (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
    (let $specializer (family-identity (DeclConst Empty))
      (let $not-a-type-family (Lam (DeclConst set) (Refl (DeclConst Empty)))
        (let $bad (try (set:native-use $package
          (Lam $type (App (App $specializer $not-a-type-family) (DeclConst Empty)))))
          (TypeFamilyRejection (car-atom $bad)))))))

; The same argument is admitted at two different internal universe domains.
; Both programs return it with reflexivity at the common, higher carrier.
; The outer pair keeps evidence indexed by the admitted source proof.
(= (universe-pair-consumer $proof-type $domain $input)
 (let $pair-type (Sigma (Sort (LevelConst 1))
                       (Id (Sort (LevelConst 1)) (idx 0) (idx 0)))
   (Lam $proof-type
    (App (Lam (Sigma $pair-type (Id $proof-type (idx 1) (idx 1))) (idx 0))
      (Pair (App (Lam $domain (Pair (idx 0) (Refl (idx 0)))) $input)
            (Refl (idx 0)))))))

(= (checked-universe-domain-pair $theorem)
 (let $package (set:native-proof $theorem)
  (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
   (let $lower (universe-pair-consumer $type (Sort (LevelConst 0)) (DeclConst set))
    (let $upper (universe-pair-consumer $type (Sort (LevelConst 1)) (DeclConst set))
     (let (SetNativeNormalFormV1 $checked-l $body-l $app-l $normal-l $source-l $type-l)
          (set:native-normalize $package $lower)
      (let (SetNativeNormalFormV1 $checked-r $body-r $app-r $normal-r $source-r $type-r)
           (set:native-normalize $package $upper)
       (UniverseDomainPair $name
         (== $checked-l $package) (== $checked-r $package)
         (== $normal-l $normal-r) (== $type-l $type-r)
         (== $normal-l (Pair (Pair (DeclConst set) (Refl (DeclConst set))) (Refl $term)))
         (size-atom $assumptions)))))))))

!(checked-universe-domain-pair pair-consumer-law)

; A universe is not an element of itself. The smaller-domain program must
; reject this input even though erasing the types would leave a beta redex.
!(let $package (set:native-proof pair-consumer-law)
 (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
  (let $invalid (universe-pair-consumer $type (Sort (LevelConst 0)) (Sort (LevelConst 0)))
   (let $bad (try (set:native-normalize $package $invalid))
    (UniverseDomainRejection (car-atom $bad))))))
