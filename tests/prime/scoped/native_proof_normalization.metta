; Compute the checked application using the existing conversion engine, then
; independently check its value at the computed dependent result type,
; retaining the original displayed type alongside its normal form.
!(set:theorem computed-id (all prop (lam p (imp p p)))
  (pf:all-intro (pf:imp-intro (pf:hyp 0))))
!(let $package (set:native-proof computed-id)
 (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
  (let (App (DeclConst $family) $formula) $type
   (let $consumer (Lam $type (App (idx 0) (DeclConst Falsum)))
    (let (SetNativeNormalFormV1 $checked $body $application $normal $original-type $result-type)
      (set:native-normalize $package $consumer)
     (NativeNormal
       (== $checked $package)
       (== $application (App $consumer $term))
       (== $normal (Lam (App (DeclConst $family) (DeclConst Falsum)) (idx 0)))
       (== $result-type (Pi (App (DeclConst $family) (DeclConst Falsum))
                           (App (DeclConst $family) (DeclConst Falsum))))))))))

; The result type still names the original compiled proof, even when the
; returned value has been computed. It is not an independently invented proof.
!(let $package (set:native-proof computed-id)
 (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
  (let $consumer (Lam $type (Refl (idx 0)))
   (let (SetNativeUseV1 $before $body-before $application-before $displayed)
     (set:native-use $package $consumer)
    (let (SetNativeNormalFormV1 $checked $body $application $normal $original-type $result-type)
      (set:native-normalize $package $consumer)
     (let (Id $carrier $left $right) $result-type
      (NativeDependentNormal
        (== $normal (Refl $term)) (== $original-type $displayed)
        (== $left $term) (== $right $term))))))))

; A dependent pair's second projection computes evidence about its first
; component. Type synthesis of this enclosing lambda already computes the
; index; executing the application must return the corresponding proof.
!(let $package (set:native-proof computed-id)
 (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
  (let $consumer
    (Lam $type
      (App (Lam (Sigma $type (Id $type (idx 0) (idx 0))) (Snd (idx 0)))
           (Pair (idx 0) (Refl (idx 0)))))
   (let (SetNativeNormalFormV1 $checked $body $application $normal $original-type $result-type)
     (set:native-normalize $package $consumer)
    (let (Id $carrier-before $left-before $right-before) $original-type
     (let (Id $carrier-after $left-after $right-after) $result-type
      (NativePairNormal
        (== $normal (Refl $term))
        (== $left-before $term)
        (== $right-before $left-before)
        (== $left-after $term) (== $right-after $term))))))))

!(let $package (set:native-proof computed-id)
 (let $bad (try (set:native-normalize $package
   (Lam (Sort (LevelConst 0)) (Refl (idx 0))))) (car-atom $bad)))
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
  (set:native-proof computed-id)
 (let $bad (try (set:native-normalize
   (SetNativeProofV1 $name $prop $proof (Refl $term) $type $context $rules $assumptions $digest)
   (Lam $type (Refl (idx 0))))) (car-atom $bad)))

; Computation does not remove an assumed premise from the returned package.
!(set:axiom computed-premise (In Empty Empty))
!(let $package (set:native-proof computed-premise)
 (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
  (let (SetNativeNormalFormV1 $checked $body $application $normal $original-type $result-type)
    (set:native-normalize $package (Lam $type (Refl (idx 0))))
   (NativeAssumedNormal (== $normal (Refl $term)) (size-atom $assumptions)))))

; A saved package cannot normalize a consumer after its premise is withdrawn.
!(let $package (set:native-proof computed-premise)
 (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
  (let $removed (match &self
    (set:known computed-premise $p $origin $tree $depends $revision $signature)
    (remove-atom &self (set:known computed-premise $p $origin $tree $depends $revision $signature)))
   (let $bad (try (set:native-normalize $package (Lam $type (Refl (idx 0)))))
    (car-atom $bad)))))
