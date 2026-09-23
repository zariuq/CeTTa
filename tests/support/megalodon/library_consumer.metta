; A later development uses the loaded source theorem as a retained premise,
; proves a new theorem, then consumes its native proof dependently.
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
       (LibraryDerivedProof
        (== $result-type
         (Pi (App (DeclConst $family) (DeclConst Falsum))
             (App (DeclConst $family) (DeclConst Falsum))))
        (size-atom $assumptions)))))))))

; The later development can compute its proof-consuming function, not only
; submit its application for checking. The result is the identity function
; on evidence of False, not evidence of False itself.
!(let $package (set:native-proof library-copy)
 (let (SetNativeProofV1 $name $formula $proof $term $type
        $context $rules $assumptions $digest) $package
  (let (App (DeclConst $family) $source-formula) $type
   (let (SetNativeNormalFormV1 $checked $consumer $application
          $normal $original-type $normal-type)
     (set:native-normalize $package (Lam $type (App (idx 0) (DeclConst Falsum))))
    (LibraryComputedProof
      (== $normal (Lam (App (DeclConst $family) (DeclConst Falsum)) (idx 0)))
      (== $normal-type (Pi (App (DeclConst $family) (DeclConst Falsum))
                          (App (DeclConst $family) (DeclConst Falsum)))))))))
