(hol:profile core)

; Retained proof of zero-addition. The theorem is hosted by the kernel
; proof package. The same predicate is the family the iterator consumes.
; Reflexivity proves the closed numeral by computation and does not prove
; the open equation. The iterator's successor step uses the supplied evidence.
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
!(add-atom &self (: n num))
!(add-atom &self (: e (id num (add zero n) n)))
!(pf:proves &self (pf:by zero-add) (eq num (add zero n) n))
!(pf:proves &self (pf:refl num n) (eq num (add zero n) n))
!(pf:proves &self (pf:by zero-add)
   (eq num (add zero (suc (suc zero))) (suc (suc zero))))
!(pf:proves &self (pf:by zero-add)
   (eq num (add zero (suc (suc zero))) zero))
!(type:eq &self
   (fst (iterCert (suc (suc zero)) num eqAt sucStep zero (refl zero)))
   (suc (suc zero)))
!(type:check &self
   (snd ((returnIter num) eqAt (suc (suc zero)) sucStep zero (refl zero)))
   (eqAt (suc (suc zero))))
!(type:check &self (refl (suc n)) (eqAt (suc n)))
!(type:eq &self (snd (sucStep n e)) (refl (suc n)))
!(let $iter (type:eq &self
     (fst (iterCert (suc (suc zero)) num eqAt sucStep zero (refl zero)))
     (suc (suc zero)))
 (let $at (type:check &self
     (snd (iterCert (suc (suc zero)) num eqAt sucStep zero (refl zero)))
     (eqAt (suc (suc zero))))
  (let $package (hol:native-proof zero-add)
   (let (HolNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
        $package
    (let (HolNativeUseV1 $checked $consumer $result $result-type)
         (hol:native-use $package (Lam $type (idx 0)))
     (HostedIteration $iter $at (== $checked $package)
       (== $prop (all num (lam k (eq num (add zero k) k))))
       (size-atom $assumptions)))))))
!(let $package (hol:native-proof zero-add)
 (let (HolNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      $package
  (let $bad (HolNativeProofV1 $name $prop $proof (Refl $term) $type
             $context $rules $assumptions $digest)
   (let $result (try (hol:native-use $bad (Lam $type (idx 0))))
    (ForgedHosted (car-atom $result))))))
