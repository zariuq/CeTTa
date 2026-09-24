; Correspondence between the executed iterator, the hosted zero-add
; package, and the candidate-syntax shared step. The successor equation stored
; by set:define is the shared application. The hosted proof term is the
; induction article of the forall equation. These are different terms.
; Bare evaluation of the iterator is not the typing derivation. Conversion
; does not compare a source term with a represented package term.
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
!(let $package (set:native-proof zero-add)
 (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      $package
  (EquationAgainstArticle
    (try (type:eq &self
       (snd (iterCert (suc (suc zero)) num eqAt sucStep zero (refl zero)))
       $term))
    (type:check &self $term (eqAt (suc (suc zero))))
    (type:check &self
       (snd (iterCert (suc (suc zero)) num eqAt sucStep zero (refl zero)))
       (eqAt (suc (suc zero))))
    (== $prop (all num (lam k (eq num (add zero k) k))))
    (== $prop (sigma (y : num) (eqAt y))))))
!(match &self (type:rule iterCert 6 ($n $A $P $step $x $e) $rhs)
   (IterEquation $rhs))
; A covered call typechecks in ordinary evaluation and does not rewrite
; `type:rule` equations there. Reduction is the typing derivation.
!(fst (iterCert (suc zero) num eqAt sucStep zero (refl zero)))
!(type:eq &self
   (fst (iterCert (suc zero) num eqAt sucStep zero (refl zero)))
   (suc zero))
; The hosted article authorizes the computed index. The id evidence at that
; index is reflexivity of the numeral, because addition computes there.
; The article itself is not that id term.
!(let $package (set:native-proof zero-add)
 (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      $package
  (let (SetNativeUseV1 $checked $consumer $result $result-type)
       (set:native-use $package (Lam $type (idx 0)))
   (let $ran (type:eq &self
        (fst (iterCert (suc (suc zero)) num eqAt sucStep zero (refl zero)))
        (suc (suc zero)))
    (let $by (pf:proves &self (pf:by zero-add)
         (eq num (add zero (suc (suc zero))) (suc (suc zero))))
     (TranslatedId
       (refl (suc (suc zero)))
       (type:check &self (refl (suc (suc zero)))
          (eqAt (suc (suc zero))))
       $ran $by (== $checked $package)
       (try (type:eq &self (refl (suc (suc zero))) $term))))))))
!(let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
      (set:native-proof zero-add)
 (let $bad (SetNativeProofV1 $name $prop $proof (Refl $term) $type
             $context $rules $assumptions $digest)
  (let $result (try (set:native-use $bad (Lam $type (idx 0))))
   (ForgedTranslation (car-atom $result)))))
; At a neutral index the hosted equation is proved, and neither reflexivity
; nor the hosted proof term inhabits `eqAt`. Transport of identity evidence
; does. That transport is not reflexivity of the successor.
!(add-atom &self (: n num))
!(add-atom &self (: e (id num (add zero n) n)))
!(let $by (pf:proves &self (pf:by zero-add) (eq num (add zero n) n))
 (let $reflEq (pf:proves &self (pf:refl num n) (eq num (add zero n) n))
  (let $reflId (type:check &self (refl n) (eqAt n))
   (let $package (set:native-proof zero-add)
    (let (SetNativeProofV1 $name $prop $proof $term $type $context $rules $assumptions $digest)
         $package
     (let $article (type:check &self $term (eqAt n))
      (let $moved (type:check &self (sucMove n e) (eqAt (suc n)))
       (NeutralIndex $by $reflEq $reflId $article $moved
         (type:eq &self (sucMove n e) (refl (suc n)))))))))))
!(try (set:define &self onIndex
   (-> (n : num) (xs : (id num n n)) num)
   (= (onIndex $n (refl $n)) zero)))
; A numeral is not identity evidence at zero. The instantiated domain is
; `(eqAt zero)`, so the binder was read as a name.
!(fst (iterCert (suc zero) num eqAt sucStep zero zero))
