; Reusable certified transformations for a supplied family.
; A step takes a value and evidence that the family holds of that value,
; computes a new value, and returns evidence that the family holds of the
; computed value. The evidence is the supplied proof or a move applied to it.
; Import this only after `num` with constructors `zero` and `suc` exists:
; iteration splits on that closed inductive.
!(set:define &self keepCert
   (-> (A : (u 0))
       (P : (-> A (u 0)))
       (x : A)
       (e : (P x))
       (sigma (y : A) (P y)))
   (= (keepCert $A $P $x $e) (pair $x $e)))
!(set:define &self transportCert
   (-> (A : (u 0))
       (P : (-> A (u 0)))
       (h : (-> A A))
       (move : (-> (x : A) (e : (P x)) (P (h x))))
       (x : A)
       (e : (P x))
       (sigma (y : A) (P y)))
   (= (transportCert $A $P $h $move $x $e) (pair ($h $x) ($move $x $e))))
!(set:define &self composeCert
   (-> (A : (u 0))
       (P : (-> A (u 0)))
       (step1 : (-> (x : A) (e : (P x)) (sigma (y : A) (P y))))
       (step2 : (-> (x : A) (e : (P x)) (sigma (y : A) (P y))))
       (x : A)
       (e : (P x))
       (sigma (z : A) (P z)))
   (= (composeCert $A $P $step1 $step2 $x $e)
      ((lam pack ($step2 (fst pack) (snd pack))) ($step1 $x $e))))
!(set:define &self iterCert
   (-> (n : num)
       (A : (u 0))
       (P : (-> A (u 0)))
       (step : (-> (x : A) (e : (P x)) (sigma (y : A) (P y))))
       (x : A)
       (e : (P x))
       (sigma (y : A) (P y)))
   (= (iterCert zero $A $P $step $x $e) (pair $x $e))
   (= (iterCert (suc $n) $A $P $step $x $e)
      ((lam pack (iterCert $n $A $P $step (fst pack) (snd pack))) ($step $x $e))))
!(set:define &self returnIter
   (-> (A : (u 0))
       (-> (P : (-> A (u 0)))
           (-> (n : num)
               (step : (-> (x : A) (e : (P x)) (sigma (y : A) (P y))))
               (x : A)
               (e : (P x))
               (sigma (y : A) (P y)))))
   (= (returnIter $A)
      (lam P (lam n (lam step (lam x (lam e (iterCert n $A P step x e))))))))
