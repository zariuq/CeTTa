(set:profile hol)

; A later development uses the loaded source theorem as a retained premise,
; proves a new theorem, then consumes its native proof dependently.  The
; signature defines Falsum as the proposition implying every proposition, as
; the source defines its own `False := forall p:prop, p`: instantiated at
; Falsum or at that code, the identity has the decoded type of
; `(forall p, p) -> (forall p, p)`.
!(let $name
 (match &self
  (MegalodonSourceInstanceV1 $name $position THM "test_assume"
    $identity $proof-id $line $types $prefix)
  $name)
 (let $proposition (set:known-proposition $name)
  (let $published (set:theorem library-copy $proposition (pf:known $name))
   (let $package (set:native-proof library-copy)
    (let (SetNativeProofV1 $label $formula $proof $term $type
           $context $rules $assumptions $digest) $package
     (let (App (DeclConst $family) $source-formula) $type
      (let (SetNativeUseV1 $checked $consumer $result $result-type)
       (set:native-use $package (Lam $type (App (idx 0) (DeclConst Falsum))))
       (let (SetNativeUseV1 $code-checked $code-consumer $code-result $code-type)
        (set:native-use $package
          (Lam $type (App (idx 0) (App (DeclConst all@prop) (Lam (DeclConst prop) (idx 0))))))
        (LibraryDerivedProof
         (== $result-type
          (Pi (Pi (DeclConst prop) (App (DeclConst $family) (idx 0)))
              (Pi (DeclConst prop) (App (DeclConst $family) (idx 0)))))
         (== $code-type $result-type)
         (size-atom $assumptions))))))))))

; The later development can compute its proof-consuming function, not only
; submit its application for checking. The result is the identity function
; on evidence of False, not evidence of False itself: its domain is evidence
; of every proposition, Falsum's decoded form, the same as at the source's
; code of False.
!(let $package (set:native-proof library-copy)
 (let (SetNativeProofV1 $name $formula $proof $term $type
        $context $rules $assumptions $digest) $package
  (let (App (DeclConst $family) $source-formula) $type
   (let (SetNativeNormalFormV1 $checked $consumer $application
          $normal $original-type $normal-type)
     (set:native-normalize $package (Lam $type (App (idx 0) (DeclConst Falsum))))
    (let (SetNativeNormalFormV1 $code-checked $code-consumer $code-application
           $code-normal $code-original-type $code-normal-type)
      (set:native-normalize $package
        (Lam $type (App (idx 0) (App (DeclConst all@prop) (Lam (DeclConst prop) (idx 0))))))
     (LibraryComputedProof
       (== $normal (Lam (Pi (DeclConst prop) (App (DeclConst $family) (idx 0))) (idx 0)))
       (== $normal-type (Pi (Pi (DeclConst prop) (App (DeclConst $family) (idx 0)))
                           (Pi (DeclConst prop) (App (DeclConst $family) (idx 0)))))
       (== $code-normal $normal)))))))
