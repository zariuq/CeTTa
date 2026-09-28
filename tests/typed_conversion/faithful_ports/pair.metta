; Product projection retains both quantified types and both quantified components.
!(bind! &thy (new-space))
!(set:define &thy mk-pair
  (-> (A : (u 0)) (B : (u 0)) (a : A) (b : B) (sigma (x : A) B))
  (= (mk-pair $A $B $a $b) (pair $a $b)))
!(set:define &thy pair-proj
  (-> (A : (u 0)) (B : (u 0)) (a : A) (b : B)
      (id A (fst (mk-pair A B a b)) a))
  (= (pair-proj $A $B $a $b) (refl $a)))
!(type:check &thy pair-proj
  (-> (A : (u 0)) (B : (u 0)) (a : A) (b : B)
      (id A (fst (mk-pair A B a b)) a)))
!(type:check &thy (lam A (lam B (lam a (lam b (refl a)))))
  (-> (A : (u 0)) (B : (u 0)) (a : A) (b : B)
      (id A (fst (mk-pair A B a b)) a)))
