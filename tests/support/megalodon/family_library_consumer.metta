(set:profile hol)

; Specialize the imported family theorem to the identity family. This is a
; new retained proof, using the source membership relation and universe map.
; The source's sets are its own base type, `mg-base-0`.
; Retrieve the universe definition's name as syntax; its value is a function.
!(let* (($fibre (match &self
           (MegalodonSourceInstanceV1 $n $p THM "family_fibre_in_universe" $i $j $l $t $d) $n))
        ($member (match &self
           (MegalodonSourceInstanceV1 $mn $mp PRIM "In" $mi $mj $ml $mt $md) $mn))
        ($universe-record (match &self
           (MegalodonSourceInstanceV1 $un $up DEF "familyUniverse" $ui $uj $ul $ut $ud)
           (MegalodonSourceInstanceV1 $un $up DEF "familyUniverse" $ui $uj $ul $ut $ud))))
 (unify $universe-record
  (MegalodonSourceInstanceV1 $universe $up DEF "familyUniverse" $ui $uj $ul $ut $ud)
  (set:theorem family-identity-fibre
  (all mg-base-0 (lam A (all mg-base-0 (lam a
    (imp (($member a) A) (($member a) (($universe A) (lam x x))))))))
  (pf:all-intro (pf:all-intro (pf:imp-intro
    (pf:imp-elim
      (pf:all-elim
        (pf:all-elim (pf:all-elim (pf:known $fibre) (pf:var 1)) (lam x x))
        (pf:var 0))
      (pf:hyp 0))))))
  FamilyUniverseMetadataMissing))

; The compiled theorem now computes a native function from a set, an element
; and its membership proof to membership in the enclosing family universe.
!(let $package (set:native-proof family-identity-fibre)
 (unify $package (SetNativeProofV1 $name $formula $proof $term $type $context $rules $assumptions $digest)
  (let $consumer (Lam $type
     (Lam (DeclConst mg-base-0) (Lam (DeclConst mg-base-0)
       (App (App (idx 2) (idx 1)) (idx 0)))))
   (let (SetNativeNormalFormV1 $checked $body $application $value $source-type $normal-type)
       (set:native-normalize $package $consumer)
    (let (Lam $set-a (Lam $set-b (Lam $premise $result))) $value
     (let (Pi $type-a (Pi $type-b (Pi $input $output))) $normal-type
      (FamilyIdentityProgram
        (noreduce-eq $set-a (DeclConst mg-base-0))
        (noreduce-eq $set-b (DeclConst mg-base-0))
        (noreduce-eq $type-a $set-a) (noreduce-eq $type-b $set-b)
        (noreduce-eq $input $premise) (size-atom $assumptions))))))
  FamilyIdentityMalformedPackage))

; The source proof cannot instead manufacture unrestricted self-membership.
!(let $member (match &self
    (MegalodonSourceInstanceV1 $n $p PRIM "In" $i $j $l $t $d) $n)
 (let $bad (try (set:theorem family-wrong-result
      (all mg-base-0 (lam a (($member a) a))) (pf:known family-identity-fibre)))
  (FamilyWrongResult (car-atom $bad))))

; A retained package cannot outlive the authority of what it declares: here
; the imported theorem it cites by name.
!(let $package (set:native-proof family-identity-fibre)
 (unify $package (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
  (let ($premise-name $premise-formula $native-name) (car-atom $assumptions)
   (let $removed (match &self
     (set:known $premise-name $p $origin $tree $depends $revision $signature)
     (remove-atom &self (set:known $premise-name $p $origin $tree $depends $revision $signature)))
    (let $result (try (set:native-normalize $package (Lam $type (Refl (idx 0)))))
     (FamilyWithdrawnAssumption (car-atom $result)))))
  FamilyWithdrawnMalformedPackage))
