; Specialize the imported family theorem to the identity family. This is a
; new retained proof, using the source membership relation and universe map.
!(let* (($fibre (match &self
           (MegalodonSourceInstanceV1 $n $p THM "family_fibre_in_universe" $i $j $l $t $d) $n))
        ($member (match &self
           (MegalodonSourceInstanceV1 $mn $mp PRIM "In" $mi $mj $ml $mt $md) $mn))
        ($universe (match &self
           (MegalodonSourceInstanceV1 $un $up DEF "familyUniverse" $ui $uj $ul $ut $ud) $un)))
 (set:theorem family-identity-fibre
  (all set (lam A (all set (lam a
    (imp (($member a) A) (($member a) (($universe A) (lam x x))))))))
  (pf:all-intro (pf:all-intro (pf:imp-intro
    (pf:imp-elim
      (pf:all-elim
        (pf:all-elim (pf:all-elim (pf:known $fibre) (pf:var 1)) (lam x x))
        (pf:var 0))
      (pf:hyp 0)))))))

; The compiled theorem now computes a native function from a set, an element
; and its membership proof to membership in the enclosing family universe.
!(let $package (set:native-proof family-identity-fibre)
 (let (SetNativeProofV1 $name $formula $proof $term $type $context $rules $assumptions $digest) $package
  (let $consumer (Lam $type
     (Lam (DeclConst set) (Lam (DeclConst set)
       (App (App (idx 2) (idx 1)) (idx 0)))))
   (let (SetNativeNormalFormV1 $checked $body $application $value $source-type $normal-type)
       (set:native-normalize $package $consumer)
    (let (Lam $set-a (Lam $set-b (Lam $premise $result))) $value
     (let (Pi $type-a (Pi $type-b (Pi $input $output))) $normal-type
      (FamilyIdentityProgram
        (== $set-a (DeclConst set)) (== $set-b (DeclConst set))
        (== $type-a $set-a) (== $type-b $set-b)
        (== $input $premise) (size-atom $assumptions))))))))

; The source proof cannot instead manufacture unrestricted self-membership.
!(let $member (match &self
    (MegalodonSourceInstanceV1 $n $p PRIM "In" $i $j $l $t $d) $n)
 (let $bad (try (set:theorem family-wrong-result
      (all set (lam a (($member a) a))) (pf:known family-identity-fibre)))
  (FamilyWrongResult (car-atom $bad))))

; A retained package cannot outlive the authority of an assumption it uses.
!(let $package (set:native-proof family-identity-fibre)
 (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest) $package
  (let ($premise-name $premise-formula $native-name) (car-atom $assumptions)
   (let $removed (match &self
     (set:known $premise-name $p $origin $tree $depends $revision $signature)
     (remove-atom &self (set:known $premise-name $p $origin $tree $depends $revision $signature)))
    (let $result (try (set:native-normalize $package (Lam $type (Refl (idx 0)))))
     (FamilyWithdrawnAssumption (car-atom $result)))))))
