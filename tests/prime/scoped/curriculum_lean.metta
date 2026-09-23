; The Lean ladder of the MettaKernel curriculum, ported statement by
; statement where the draft's set: language can state it.  Booleans and
; naturals are inductive; the connectives are definitions; the ladder's
; negative controls are refusals here.
!(import! &self pf)
!(bind! &thy (new-space))
!(set:inductive &thy bool (u 0) (: true bool) (: false bool))
!(set:inductive &thy nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(pf:equality &thy bool refl@bool subst@bool)
!(pf:equality &thy nat refl@nat subst@nat)
!(pf:equality &thy (-> nat nat) refl@fn subst@fn)

; ---- 01 basics: negb, andb, double; negb_negb by cases; double_two by computation
!(set:define &thy negb (-> bool bool) (= (negb true) false) (= (negb false) true))
!(set:define &thy andb (-> bool bool bool) (= (andb true $y) $y) (= (andb false $y) false))
!(set:define &thy add (-> nat nat nat) (= (add $n zero) $n) (= (add $n (succ $m)) (succ (add $n $m))))
!(set:define &thy double (-> nat nat) (= (double zero) zero) (= (double (succ $n)) (succ (succ (double $n)))))
!(pf:theorem &thy negb-negb (all bool (lam b (eq bool (negb (negb b)) b)))
   (pf:induction bool-ind (lam b (eq bool (negb (negb b)) b)) (pf:refl bool true) (pf:refl bool false)))
!(pf:theorem &thy double-two (eq nat (double (succ (succ zero))) (succ (succ (succ (succ zero)))))
   (pf:refl nat (succ (succ (succ (succ zero))))))

; ---- 03 propositions: the connectives as definitions, the ladder's theorems
!(set:define &thy truth prop (= truth (all prop (lam p (imp p p)))))
!(set:define &thy falsum prop (= falsum (all prop (lam p p))))
!(set:define &thy neg (-> prop prop) (= (neg $p) (imp $p falsum)))
!(set:define &thy and (-> prop prop prop) (= (and $p $q) (all prop (lam r (imp (imp $p (imp $q r)) r)))))
!(set:define &thy or (-> prop prop prop) (= (or $p $q) (all prop (lam r (imp (imp $p r) (imp (imp $q r) r))))))
!(set:define &thy exists-nat (-> (-> nat prop) prop) (= (exists-nat $P) (all prop (lam r (imp (all nat (lam x (imp ($P x) r))) r)))))
!(pf:theorem &thy imp-self (all prop (lam p (imp p p))) (pf:fix p (pf:assume hp hp)))
!(pf:theorem &thy imp-trans (all prop (lam p (all prop (lam q (all prop (lam r (imp (imp p q) (imp (imp q r) (imp p r)))))))))
   (pf:fix p (pf:fix q (pf:fix r (pf:assume f (pf:assume g (pf:assume hp (pf:by g (pf:by f hp)))))))))
!(pf:theorem &thy and-comm (all prop (lam p (all prop (lam q (imp (and p q) (and q p))))))
   (pf:fix p (pf:fix q (pf:assume h (pf:fix r (pf:assume k (pf:by h (pf:assume hp (pf:assume hq (pf:by k hq hp))))))))))
!(pf:theorem &thy or-comm (all prop (lam p (all prop (lam q (imp (or p q) (or q p))))))
   (pf:fix p (pf:fix q (pf:assume h (pf:fix r (pf:assume kq (pf:assume kp (pf:by h kp kq))))))))
!(pf:theorem &thy not-not-intro (all prop (lam p (imp p (neg (neg p)))))
   (pf:fix p (pf:assume hp (pf:assume hnp (pf:by hnp hp)))))
!(pf:theorem &thy false-elim (all prop (lam p (imp falsum p)))
   (pf:fix p (pf:assume h (pf:by h))))
!(pf:theorem &thy exists-zero (exists-nat (lam n (eq nat n zero)))
   (pf:fix r (pf:assume k (pf:imp-elim (pf:all-elim k zero) (pf:refl nat zero)))))
!(pf:theorem &thy exists-elim
   (all (-> nat prop) (lam P (all prop (lam q (imp (exists-nat P) (imp (all nat (lam n (imp (P n) q))) q))))))
   (pf:fix P (pf:fix q (pf:assume h (pf:assume k (pf:by h k))))))

; ---- 04 equality: symmetry, transitivity, congruence, substitution, computation
!(pf:theorem &thy eq-compute (eq nat (add (succ (succ zero)) (succ (succ zero))) (succ (succ (succ (succ zero)))))
   (pf:refl nat (succ (succ (succ (succ zero))))))
!(pf:theorem &thy eq-symm (all nat (lam a (all nat (lam b (imp (eq nat a b) (eq nat b a))))))
   (pf:fix a (pf:fix b (pf:assume h (pf:sym nat a b h)))))
!(pf:theorem &thy eq-trans (all nat (lam a (all nat (lam b (all nat (lam c (imp (eq nat a b) (imp (eq nat b c) (eq nat a c)))))))))
   (pf:fix a (pf:fix b (pf:fix c (pf:assume h1 (pf:assume h2 (pf:trans nat a b c h1 h2)))))))
!(pf:theorem &thy congr-succ (all nat (lam a (all nat (lam b (imp (eq nat a b) (eq nat (succ a) (succ b)))))))
   (pf:fix a (pf:fix b (pf:assume h (pf:cong nat nat (lam x (succ x)) a b h)))))
; subst_pred is the substitution axiom itself, used through pf:by
!(pf:theorem &thy subst-pred (all nat (lam a (all nat (lam b (all (-> nat prop) (lam P (imp (eq nat a b) (imp (P a) (P b)))))))))
   (pf:fix a (pf:fix b (pf:fix P (pf:assume h (pf:assume pa (pf:subst-with nat P a b h pa)))))))

; ---- 07 recursors: the definition is the recursor; conv_add_zero by computation under the binder
!(pf:theorem &thy add-zero-fun (eq (-> nat nat) (lam x (add x zero)) (lam x x))
   (pf:refl (-> nat nat) (lam x x)))

; ---- 10 tactics, 11 conv: what tactics compute here, conversion computes
!(pf:theorem &thy zero-add (all nat (lam n (eq nat (add zero n) n)))
   (pf:induction nat-ind (lam n (eq nat (add zero n) n))
     (pf:refl nat zero)
     (pf:fix k (pf:assume ih (pf:cong nat nat (lam r (succ r)) (add zero k) k ih)))))
!(pf:theorem &thy conv-in-hyp (all nat (lam a (all nat (lam b (imp (eq nat (add a zero) b) (eq nat a b))))))
   (pf:fix a (pf:fix b (pf:assume h h))))
!(set:define &thy mul (-> nat nat nat) (= (mul $n zero) zero) (= (mul $n (succ $m)) (add (mul $n $m) $n)))
!(pf:theorem &thy conv-in-hyp2 (all nat (lam a (all nat (lam b (imp (eq nat (mul a (succ zero)) b) (eq nat a b))))))
   (pf:fix a (pf:fix b (pf:assume h (pf:trans nat a (add zero a) b (pf:sym nat (add zero a) a (pf:by zero-add)) h)))))

; ---- 14 eta: a function is its own eta-expansion
!(pf:theory &thy ((: f (-> nat nat))))
!(pf:theorem &thy eta (eq (-> nat nat) f (lam x (f x))) (pf:refl (-> nat nat) f))

; ---- 15 impredicativity: the definition of truth is the ladder's ImpredTrue; funext is declared, not proved
!(pf:theorem &thy impred-true truth (pf:fix p (pf:assume hp hp)))
!(set:axiom &thy funext (all (-> nat nat) (lam f (all (-> nat nat) (lam g (imp (all nat (lam x (eq nat (f x) (g x)))) (eq (-> nat nat) f g)))))))

; ---- negative controls of the ladder
!(pf:proves &thy (pf:refl nat zero) (eq nat zero (succ zero)))
!(set:check &thy (succ true) nat)
!(set:define &thy partial (-> bool nat) (= (partial true) zero))
!(let $r (try (set:proves &thy (pf:fix P (pf:fix Q (pf:assume h h))) (all prop (lam P (all prop (lam Q (imp P Q))))))) (car-atom $r))
!(set:check &thy true nat)
