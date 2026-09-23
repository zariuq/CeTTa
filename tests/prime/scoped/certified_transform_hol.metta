; Ordinary arithmetic through the certified-transformation library.
; Addition recurses on its second argument, so `add zero n = n` is not
; definitional at a neutral `n`. The supplied family is that equation.
; Successor transport uses the incoming evidence. Reflexivity of the
; successor does not.
!(import! &self pf)
!(set:inductive &self num (u 0) (: zero num) (: suc (-> num num)))
!(pf:equality &self num refl@num subst@num)
!(set:define &self add (-> num num num)
   (= (add $n zero) $n)
   (= (add $n (suc $m)) (suc (add $n $m))))
!(set:define &self dbl (-> num num)
   (= (dbl zero) zero)
   (= (dbl (suc $n)) (suc (suc (dbl $n)))))
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
!(add-atom &self (: n num))
!(add-atom &self (: e (id num (add zero n) n)))

; Doubling is an ordinary HOL computation. The library's evidence is the
; transported addition equation, indexed by the computed successor.
!(pf:proves &self (pf:refl num (suc (suc (suc (suc zero)))))
   (eq num (dbl (suc (suc zero))) (suc (suc (suc (suc zero))))))
!(type:eq &self
   (fst (composeCert num eqAt sucStep sucStep zero (refl zero)))
   (suc (suc zero)))
!(type:check &self
   (snd (composeCert num eqAt sucStep sucStep zero (refl zero)))
   (eqAt (suc (suc zero))))
!(type:eq &self
   (fst (iterCert (suc (suc zero)) num eqAt sucStep zero (refl zero)))
   (suc (suc zero)))
!(type:eq &self
   (fst ((returnIter num) eqAt (suc zero) sucStep (add (suc zero) zero)
         (refl (add (suc zero) zero))))
   (suc (suc zero)))
!(type:check &self
   (iterCert (suc zero) num (lam k (eqAt (add k zero))) sucStep zero (refl zero))
   (sigma (y : num) (eqAt y)))

; At a neutral index the addition equation is not reflexivity, and a
; reflexivity proof of the wrong numeral does not prove the doubling.
!(type:check &self (refl (suc n)) (eqAt (suc n)))
!(type:eq &self (snd (sucStep n e)) (refl (suc n)))
!(type:check &self (snd (sucStep n e)) (eqAt (suc n)))
!(pf:proves &self (pf:refl num (suc zero))
   (eq num (dbl (suc zero)) (suc (suc zero))))
