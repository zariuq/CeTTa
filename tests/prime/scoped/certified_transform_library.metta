; Certified transformations for a supplied family. The family here is the
; predicate of zero-addition, which is not definitional in the second
; argument. A step must use the incoming evidence. Reflexivity of the
; successor does not inhabit that predicate at a neutral index.
!(bind! &thy (new-space))
!(set:inductive &thy num (u 0) (: zero num) (: suc (-> num num)))
!(set:define &thy add (-> num num num)
   (= (add $n zero) $n)
   (= (add $n (suc $m)) (suc (add $n $m))))
!(set:define &thy eqAt (-> num (u 0))
   (= (eqAt $n) (id num (add zero $n) $n)))
!(set:define &thy sucMove
   (-> (n : num) (e : (eqAt n)) (eqAt (suc n)))
   (= (sucMove $n $e)
      (id:eliminate num (add zero $n)
         (lam y (lam p (id num (suc (add zero $n)) (suc y))))
         (refl (suc (add zero $n)))
         $n $e)))
!(set:define &thy keepCert
   (-> (A : (u 0)) (P : (-> A (u 0))) (x : A) (e : (P x))
       (sigma (y : A) (P y)))
   (= (keepCert $A $P $x $e) (pair $x $e)))
!(set:define &thy transportCert
   (-> (A : (u 0)) (P : (-> A (u 0))) (h : (-> A A))
       (move : (-> (x : A) (e : (P x)) (P (h x))))
       (x : A) (e : (P x))
       (sigma (y : A) (P y)))
   (= (transportCert $A $P $h $move $x $e) (pair ($h $x) ($move $x $e))))
!(set:define &thy sucStep
   (-> (x : num) (e : (eqAt x)) (sigma (y : num) (eqAt y)))
   (= (sucStep $x $e) (transportCert num eqAt suc sucMove $x $e)))
!(set:define &thy composeCert
   (-> (A : (u 0)) (P : (-> A (u 0)))
       (step1 : (-> (x : A) (e : (P x)) (sigma (y : A) (P y))))
       (step2 : (-> (x : A) (e : (P x)) (sigma (y : A) (P y))))
       (x : A) (e : (P x))
       (sigma (z : A) (P z)))
   (= (composeCert $A $P $step1 $step2 $x $e)
      ((lam pack ($step2 (fst pack) (snd pack))) ($step1 $x $e))))
!(set:define &thy iterCert
   (-> (n : num) (A : (u 0)) (P : (-> A (u 0)))
       (step : (-> (x : A) (e : (P x)) (sigma (y : A) (P y))))
       (x : A) (e : (P x))
       (sigma (y : A) (P y)))
   (= (iterCert zero $A $P $step $x $e) (pair $x $e))
   (= (iterCert (suc $n) $A $P $step $x $e)
      ((lam pack (iterCert $n $A $P $step (fst pack) (snd pack))) ($step $x $e))))
!(set:define &thy returnIter
   (-> (A : (u 0))
       (-> (P : (-> A (u 0)))
           (-> (n : num)
               (step : (-> (x : A) (e : (P x)) (sigma (y : A) (P y))))
               (x : A) (e : (P x))
               (sigma (y : A) (P y)))))
   (= (returnIter $A)
      (lam P (lam n (lam step (lam x (lam e (iterCert n $A P step x e))))))))

!(add-atom &thy (: n num))
!(add-atom &thy (: e (id num (add zero n) n)))

; The supplied evidence is returned at the same index, and two successor
; steps share one package. The closure is the iterator returned as a value.
!(type:eq &thy (snd (keepCert num eqAt n e)) e)
!(type:check &thy (composeCert num eqAt sucStep sucStep zero (refl zero))
   (sigma (z : num) (eqAt z)))
!(type:eq &thy (fst (composeCert num eqAt sucStep sucStep zero (refl zero)))
   (suc (suc zero)))
!(type:check &thy (snd (composeCert num eqAt sucStep sucStep zero (refl zero)))
   (eqAt (suc (suc zero))))
!(type:eq &thy (fst (iterCert (suc (suc zero)) num eqAt sucStep zero (refl zero)))
   (suc (suc zero)))
!(type:eq &thy (fst ((returnIter num) eqAt (suc zero) sucStep zero (refl zero)))
   (suc zero))

; The family and the argument are computed, not names or prebuilt certificates.
!(type:check &thy
   (iterCert (suc zero) num (lam k (eqAt (add k zero))) sucStep zero (refl zero))
   (sigma (y : num) (eqAt y)))
!(type:eq &thy
   (fst (sucStep (add (suc zero) zero) (refl (add (suc zero) zero))))
   (suc (suc zero)))
!(type:check &thy
   (snd (sucStep (add (suc zero) zero) (refl (add (suc zero) zero))))
   (eqAt (suc (suc zero))))

; Reflexivity of the successor, a proof of the input index, and exhaustion.
!(type:check &thy (refl (suc n)) (eqAt (suc n)))
!(type:eq &thy (snd (sucStep n e)) (refl (suc n)))
!(type:check &thy (keepCert num eqAt n (refl n)) (sigma (y : num) (eqAt y)))
!(type:check &thy (iterCert (suc zero) num eqAt sucStep zero (refl (suc zero)))
   (sigma (y : num) (eqAt y)))
!(type:check &thy (iterCert (suc zero) num eqAt sucStep zero (refl zero))
   (sigma (y : num) (eqAt y)) 1)

; A family that reduces to a closed inductive is covered by that inductive.
; One defined as another such family is too. A neutral application that
; does not reduce, and an identity indexed by an earlier argument, are not.
!(set:define &thy Fam (-> num (u 0)) (= (Fam $n) num))
!(set:define &thy onFam
   (-> (n : num) (xs : (Fam n)) num)
   (= (onFam $n zero) $n)
   (= (onFam $n (suc $k)) $n))
!(type:eq &thy (onFam (suc zero) zero) (suc zero))
!(set:define &thy Via (-> num (u 0)) (= (Via $n) (Fam $n)))
!(set:define &thy onVia
   (-> (n : num) (xs : (Via n)) num)
   (= (onVia $n zero) $n)
   (= (onVia $n (suc $k)) $n))
!(type:eq &thy (onVia zero (suc zero)) zero)
!(set:define &thy IdAt (-> num (u 0)) (= (IdAt $n) (id num $n $n)))
!(try (set:define &thy onIdAt
   (-> (n : num) (xs : (IdAt n)) num)
   (= (onIdAt $n zero) zero)
   (= (onIdAt $n (suc $k)) zero)))
!(set:define &thy StuckFam (-> num (u 0))
   (= (StuckFam zero) num)
   (= (StuckFam (suc $k)) num))
!(try (set:define &thy onStuck
   (-> (n : num) (xs : (StuckFam n)) num)
   (= (onStuck $n zero) zero)
   (= (onStuck $n (suc $k)) zero)))
!(try (set:define &thy onClosedId
   (-> (n : num) (xs : (id num zero zero)) num)
   (= (onClosedId $n (refl zero)) zero)))
!(try (set:define &thy onIndex
   (-> (n : num) (xs : (id num n n)) num)
   (= (onIndex $n (refl $n)) zero)))
