; Group annotations share their preceding context; placement crosses siblings.
!(type:formed (-> (A : (u 0)) (x y : A) A))
!(type:check (lam A (lam (x y) x)) (-> (A : (u 0)) (x y : A) A))
!(type:check (lam A (lam (x y) y)) (-> (A : (u 0)) (x y : A) A))
!(type:eq (-> (A : (u 0)) (x y : A) A)
           (-> (A : (u 0)) (x : A) (y : A) A))
!(type:formed (-> (A : (u 0)) (f g : (-> (z : A) (id A z z))) A))
!(type:formed (sigma (A : (u 0)) (x y : A) A))
!(type:check (lam A (lam (x y) A)) (-> (A : (u 0)) (x y : A) A))
; The second A must be the outer type variable, not the newly bound value x.
!(type:formed (-> (A : (u 0)) (x : A) (y : x) A))

!(bind! &thy (new-space))
!(add-atom &thy (: A (u 0)))
!(add-atom &thy (: a A))
!(add-atom &thy (: b A))
!(add-atom &thy (: B (u 0)))
!(add-atom &thy (: c B))
!(set:define &thy groupedKeep (-> (A : (u 0)) (x y : A) A)
   (= (groupedKeep $A $x $y) $x))
!(type:check &thy (groupedKeep A a b) A)
!(type:eq &thy (groupedKeep A a b) a)
!(type:eq &thy (groupedKeep A a b) b)
!(type:check &thy (groupedKeep A a c) A)

; A different annotation per sibling still uses the same preceding context.
!(set:define &thy groupedChoose (-> (A B : (u 0)) (x y : A B) B)
   (= (groupedChoose $A $B $x $y) $y))
!(type:check &thy (groupedChoose A B a c) B)
!(type:eq &thy (groupedChoose A B a c) c)
!(type:check &thy (groupedChoose A B c a) B)

; Dependent identity evidence is built, returned, checked, and reused in code.
!(set:define &thy groupedEvidence
   (-> (A : (u 0)) (x y : A) (id A x x))
   (= (groupedEvidence $A $x $y) (refl $x)))
!(type:check &thy (groupedEvidence A a b) (id A a a))
!(type:eq &thy (groupedEvidence A a b) (refl a))
!(type:check &thy (groupedEvidence A a b) (id A b b))
!(set:define &thy retainEvidence
   (-> (A : (u 0)) (x : A) (p q : (id A x x)) (id A x x))
   (= (retainEvidence $A $x $p $q) $p))
!(type:check &thy
   (retainEvidence A a (groupedEvidence A a b) (refl a)) (id A a a))
!(type:eq &thy
   (retainEvidence A a (groupedEvidence A a b) (refl a)) (refl a))
!(type:check &thy
   (retainEvidence A b (groupedEvidence A a b) (refl b)) (id A b b))
