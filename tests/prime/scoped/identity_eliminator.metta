; The identity eliminator is declared by the language with the fixed
; Paulin-Mohring type and computes by one iota rule: elimination at
; reflexivity yields the method.  Nothing here decides uniqueness of
; identity proofs; a non-reflexivity path stays neutral.

!(bind! &s (new-space))
!(add-atom &s (: Nat (u 0)))
!(add-atom &s (: z Nat))
!(add-atom &s (: s (-> Nat Nat)))
!(add-atom &s (: motive (-> (y : Nat) (-> (id Nat z y) (u 0)))))
!(add-atom &s (: base (motive z (refl z))))

!(type:of &s (id:eliminate Nat z motive base z (refl z)))
!(type:check &s (id:eliminate Nat z motive base z (refl z)) (motive z (refl z)))
!(type:eq &s (id:eliminate Nat z motive base z (refl z)) base)

; A declared path that is not reflexivity does not compute.
!(add-atom &s (: q (id Nat z (s z))))
!(type:of &s (id:eliminate Nat z motive base (s z) q))
!(type:eq &s (id:eliminate Nat z motive base (s z) q) base)

; Transport: a vector indexed by its length moves along an identity.
!(add-atom &s (: Vec (-> Nat (u 0))))
!(add-atom &s (: v (Vec z)))
!(type:of &s (id:eliminate Nat z (lam y (lam e (Vec y))) v z (refl z)))
!(type:eq &s (id:eliminate Nat z (lam y (lam e (Vec y))) v z (refl z)) v)
!(type:of &s (id:eliminate Nat z (lam y (lam e (Vec y))) v (s z) q))

; A mismatched endpoint or a non-path in the path position is refuted.
!(type:check &s (id:eliminate Nat z motive base (s z) (refl z)) (motive (s z) (refl z)))
!(type:check &s (id:eliminate Nat z motive base z z) (motive z (refl z)))

; A length-changing operation must return the vector at its new index.
; The existential package keeps that index available to later consumers.
!(set:define &s moveVector
   (-> (n : Nat) (w : (Vec n)) (m : Nat) (p : (id Nat n m))
       (sigma (k : Nat) (Vec k)))
   (= (moveVector $n $w $m $p)
      (pair $m (id:eliminate Nat $n (lam k (lam e (Vec k))) $w $m $p))))
!(set:define &s readVector
   (-> (package : (sigma (k : Nat) (Vec k))) (Vec (fst package)))
   (= (readVector $package) (snd $package)))
!(type:check &s (moveVector z v z (refl z)) (sigma (k : Nat) (Vec k)))
!(type:eq &s (readVector (moveVector z v z (refl z))) v)
; A neutral path supports transport without erasing its identity evidence.
!(type:check &s (readVector (moveVector z v (s z) q)) (Vec (s z)))
!(type:eq &s (fst (moveVector z v (s z) q)) (s z))
!(type:eq &s (readVector (moveVector z v (s z) q)) v)
; The returned function retains its input vector across another binder.
!(set:define &s keepTransported
   (-> (n : Nat) (w : (Vec n)) (-> Nat (Vec n)))
   (= (keepTransported $n $w)
      (lam ignored (readVector (moveVector $n $w $n (refl $n))))))
!(type:check &s (keepTransported z v (s z)) (Vec z))
!(type:eq &s (keepTransported z v (s z)) v)
; A wrong endpoint, a wrong input fibre and a wrong output fibre fail.
!(type:check &s (moveVector z v (s z) (refl z)) (sigma (k : Nat) (Vec k)))
!(type:check &s (moveVector (s z) v (s z) (refl (s z))) (sigma (k : Nat) (Vec k)))
!(type:check &s (readVector (moveVector z v (s z) q)) (Vec z))

; The carrier and motive need not live at the same universe level.
; This motive returns U0, hence lives in U1; its method is a type.
!(type:check &s (lam y (lam e (u 0)))
   (-> (y : Nat) (e : (id Nat z y)) (u 1)))
!(type:check &s (lam y (lam e (u 0)))
   (-> (y : Nat) (e : (id Nat z y)) (u 0)))
!(set:define &s transportedType
   (-> (A : (u 0)) (n : Nat) (m : Nat) (p : (id Nat n m)) (u 0))
   (= (transportedType $A $n $m $p)
      (id:eliminate Nat $n (lam y (lam e (u 0))) $A $m $p)))
; The result type itself computes by J before this body can be accepted.
!(set:define &s valueAtTransportedType
   (-> (A : (u 0)) (value : A) (transportedType A z z (refl z)))
   (= (valueAtTransportedType $A $value) $value))
!(type:check &s (transportedType Nat z z (refl z)) (u 0))
!(type:eq &s (transportedType Nat z z (refl z)) Nat)
!(type:check &s (valueAtTransportedType Nat z) Nat)
!(type:eq &s (valueAtTransportedType Nat z) z)
; Partial application retains the type-valued method for later use.
!(type:eq &s ((id:eliminate Nat z (lam y (lam e (u 0))) (Vec z)) z (refl z)) (Vec z))
!(type:eq &s (transportedType Nat z (s z) q) Nat)
!(type:check &s (transportedType (u 0) z z (refl z)) (u 0))
!(type:check &s (valueAtTransportedType Nat v) Nat)
