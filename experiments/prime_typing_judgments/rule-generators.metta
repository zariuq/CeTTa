(hol:profile base)
!(import! &self judgments.metta)
!(hol:inductive &self Nat (u 0) (: Z Nat) (: S (-> Nat Nat)))
; Existing : and = remain the authored language, inside inspectable data.
(= (unit-program $name)
   (quote ((: $name (-> Nat Nat Nat))
           (= ($name $n Z) $n)
           (= ($name $n (S $m)) (S ($name $n $m))))){$name})
!(let $program (unit-program plus) (assertEqual (admit-block $program) plus))
!(assertEqual (plus (S Z) (S Z)) (S (S Z)))
!(assertEqual (judge (quote (|- ((: n Nat)) (convert (plus n Z) n Nat)))) True)
; Generated rule bodies must have exactly the declared result type.
!(assertEqual (car-atom (block-outcome
   (quote ((: wrong (-> Nat Nat)) (= (wrong $x) (refl $x)))))) Refuted)
!(assertEqual (car-atom (try (type:check wrong (-> Nat Nat)))) Undetermined)
; The prototype builder accepts arbitrary function names, including names
; that would be captured by a fixed x/y/e template.
!(let* (($signature (increment-signature x)) ($equations (increment-equations x)))
   (assertEqual (admit-definition $signature $equations) x))
!(let $goal (congruence-judgment x Nat Nat) (assertEqual (judge $goal) True))
!(let* (($signature (increment-signature rule-x-0)) ($equations (increment-equations rule-x-0)))
   (assertEqual (admit-definition $signature $equations) rule-x-0))
!(let $goal (congruence-judgment rule-x-0 Nat Nat) (assertEqual (judge $goal) True))
!(hol:inductive &self Bit (u 0) (: Yes Bit) (: No Bit))
!(let $goal (congruence-judgment x Bit Nat) (assertEqual (judge $goal) False))
; A conventional sequent spelling and ordinary dependent source check alike.
!(assertEqual
   (judge (quote (|- ((: n Nat) (: value (id Nat n n))) (: value (id Nat (plus n Z) n)))))
   (type:check (lam n (lam value value))
     (-> (n : Nat) (value : (id Nat n n)) (id Nat (plus n Z) n))))
; An explicitly supplied primitive equation remains an observable assumption.
(: opaque (-> Nat Nat))
(: opaque-unit (-> (n : Nat) (id Nat (opaque n) n)))
!(assertEqual (type:check (opaque-unit Z) (id Nat (opaque Z) Z)) True)
!(assertEqual (judge (quote (|- () (convert (opaque Z) Z Nat)))) False)
; The checked congruence function changes evidence, not judgmental equality.
!(let $goal (congruence-judgment opaque Nat Nat)
   (assertEqual (publish-judgment opaque-congr $goal) opaque-congr))
!(assertEqual (type:check (opaque-congr Z Z (refl Z))
   (id Nat (opaque Z) (opaque Z))) True)
!(assertEqual (judge (quote (|- () (convert (opaque Z) Z Nat)))) False)
