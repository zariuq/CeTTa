(hol:profile core)

; Revision controls for a certified transformation whose evidence is the
; hosted zero-addition theorem. The native package is rechecked by the
; existing authority. Replacing an addition rule, or forging the package,
; removes that authority. An unrelated definition does not. Removing the
; iterator's rules makes the old computation fail. A one-step budget stays
; unresolved: that is exhaustion, not a proof that the type is empty.
!(import! &self pf)
!(hol:inductive &self num (u 0) (: zero num) (: suc (-> num num)))
!(pf:equality &self num refl@num subst@num)
!(hol:known-proposition &self num-ind)
!(hol:define &self add (-> num num num)
   (= (add $n zero) $n)
   (= (add $n (suc $m)) (suc (add $n $m))))
!(hol:define &self eqAt (-> num (u 0))
   (= (eqAt $n) (id num (add zero $n) $n)))
!(hol:define &self sucMove
   (-> (n : num) (e : (eqAt n)) (eqAt (suc n)))
   (= (sucMove $n $e)
      (id:eliminate num (add zero $n)
         (lam y (lam p (id num (suc (add zero $n)) (suc y))))
         (refl (suc (add zero $n)))
         $n $e)))
!(import! &self ./certified_transform_lib.metta)
!(hol:define &self sucStep
   (-> (x : num) (e : (eqAt x)) (sigma (y : num) (eqAt y)))
   (= (sucStep $x $e) (transportCert num eqAt suc sucMove $x $e)))
!(pf:theorem &self zero-add (all num (lam k (eq num (add zero k) k)))
   (pf:induction num-ind (lam k (eq num (add zero k) k))
     (pf:refl num zero)
     (pf:fix k (pf:assume ih (pf:cong num num (lam r (suc r)) (add zero k) k ih)))))
!(let $iter (type:eq &self
     (fst (iterCert (suc zero) num eqAt sucStep zero (refl zero))) (suc zero))
 (let $at (type:check &self
     (snd (iterCert (suc zero) num eqAt sucStep zero (refl zero)))
     (eqAt (suc zero)))
  (let $package (hol:native-proof zero-add)
   (let (HolNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
        $package
    (let (HolNativeUseV1 $checked $consumer $result $result-type)
         (hol:native-use $package (Lam $type (idx 0)))
     (EnabledConsumer $iter $at (== $checked $package)
       (== $prop (all num (lam k (eq num (add zero k) k))))
       (size-atom $assumptions)))))))
!(eqAt (add zero (suc (suc zero))))
!(let $package (hol:native-proof zero-add)
 (let $linked (hol:native-link identity $package)
  (let (HolNativeLinkedV1 $interpretation $checked $term $type $context $rules $rows) $linked
   (let (HolNativeUseV1 $used $consumer $application $result-type)
        (hol:native-use $linked (Lam $type (idx 0)))
    (LinkedType $result-type)))))
!(type:eq &self
   (fst (iterCert (suc zero) num eqAt sucStep zero (refl zero))) zero)
!(let $package (hol:native-proof zero-add)
 (let (HolNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      $package
  (let $added (hol:define unused-prop prop (= unused-prop (imp Falsum Falsum)))
   (let (HolNativeUseV1 $checked $consumer $result $result-type)
        (hol:native-use $package (Lam $type (idx 0)))
    (UnrelatedRevision (== $checked $package))))))
!(let (HolNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      (hol:native-proof zero-add)
 (let $bad (HolNativeProofV1 $name $prop $proof (Refl $term) $type
             $context $rules $assumptions $digest)
  (let $result (try (hol:native-use $bad (Lam $type (idx 0))))
    (ForgedProof (car-atom $result)))))
!(let $package (hol:native-proof zero-add)
 (let (HolNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      $package
  (let $linked (hol:native-link identity $package)
   (let $changed (match &self (type:rule add $arity $patterns $rhs)
         (let $removed (remove-atom &self (type:rule add $arity $patterns $rhs))
           (add-atom &self (type:rule add $arity $patterns (DeclConst zero)))))
    (let $result (try (hol:native-use $package (Lam $type (idx 0))))
     (let $linked-result (try (hol:native-use $linked (Lam $type (idx 0))))
      (WithdrawnSupport (car-atom $result) (car-atom $linked-result))))))))
!(type:check &self (iterCert (suc zero) num eqAt sucStep zero (refl zero))
   (sigma (y : num) (eqAt y)) 1)
!(let $removed (match &self (type:rule iterCert $arity $patterns $rhs)
      (remove-atom &self (type:rule iterCert $arity $patterns $rhs)))
 (LibraryRuleWithdrawn
   (type:eq &self
      (fst (iterCert (suc zero) num eqAt sucStep zero (refl zero)))
      (suc zero))))
