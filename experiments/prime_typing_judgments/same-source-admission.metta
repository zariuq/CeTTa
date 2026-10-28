(hol:profile base)
!(hol:inductive &self Nat (u 0) (: Z Nat) (: S (-> Nat Nat)))
(: authored (-> Nat Nat))
(= (authored $n) (S $n))
!(assertEqual (authored Z) (S Z))
; A colon declaration already occupies the name. The current declaration
; API does not upgrade it by checking the accompanying ordinary equations.
!(assertEqual (car-atom (try (hol:define authored (-> Nat Nat)
   (= (authored $n) (S $n))))) Refuted)
!(assertEqual (type:check (refl (authored Z)) (id Nat (authored Z) (S Z))) False)
