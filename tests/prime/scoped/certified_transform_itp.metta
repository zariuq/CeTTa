; Retained proof of zero-addition. The theorem is hosted by the kernel
; proof package. The same predicate is the family the iterator consumes.
; Reflexivity proves the closed numeral by computation and does not prove
; the open equation. The iterator's successor step uses the supplied evidence.
!(import! &self pf)
!(set:inductive &self num (u 0) (: zero num) (: suc (-> num num)))
!(pf:equality &self num refl@num subst@num)
!(set:known-proposition &self num-ind)
!(set:define &self add (-> num num num)
   (= (add $n zero) $n)
   (= (add $n (suc $m)) (suc (add $n $m))))
!(set:define &self eqAt (-> num (u 0))
   (= (eqAt $n) (id num (add zero $n) $n)))
!(set:define &self sucMove
   (-> (n : num) (e : (eqAt n)) (eqAt (suc n)))
   (= (sucMove $n $e)
      (id:eliminate num (add zero $n)
         (lam y (lam p (id num (suc (add zero $n)) (suc y))))
         (refl (suc (add zero $n)))
         $n $e)))
!(import! &self ./certified_transform_lib.metta)
!(set:define &self sucStep
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
  (let $package (set:native-proof zero-add)
   (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
        $package
    (let (SetNativeUseV1 $checked $consumer $result $result-type)
         (set:native-use $package (Lam $type (idx 0)))
     (HostedIteration $iter $at (== $checked $package)
       (== $prop (all num (lam k (eq num (add zero k) k))))
       (size-atom $assumptions)))))))
!(let $package (set:native-proof zero-add)
 (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      $package
  (let $bad (SetNativeProofV1 $name $prop $proof (Refl $term) $type
             $context $rules $assumptions $digest)
   (let $result (try (set:native-use $bad (Lam $type (idx 0))))
    (ForgedHosted (car-atom $result))))))
