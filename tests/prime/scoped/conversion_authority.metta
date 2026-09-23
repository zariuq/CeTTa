; Existential introduction substitutes a predicate lambda beneath a proof
; binder.  Its beta redex must reach the same conversion authority as the
; corresponding closed proposition.
!(import! &self pf)
!(bind! &thy (new-space))
!(set:inductive &thy nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(pf:equality &thy nat refl@nat subst@nat)
!(set:define &thy exists-nat (-> (-> nat prop) prop)
   (= (exists-nat $P)
      (all prop (lam r (imp (all nat (lam x (imp ($P x) r))) r)))))
!(try (set:proves &thy
   (pf:fix r (pf:assume k
     (pf:imp-elim (pf:all-elim k zero)
       (pf:all-elim (pf:known refl@nat) zero))))
   (exists-nat (lam n (eq nat n zero)))))

; The same proof is not a proof of this target, even though that target
; has another witness (succ zero).  Refuted here qualifies the submitted
; proof judgment, not the truth of the proposition.
!(let $result (try (set:proves &thy
   (pf:fix r (pf:assume k
     (pf:imp-elim (pf:all-elim k zero)
       (pf:all-elim (pf:known refl@nat) zero))))
   (exists-nat (lam n (eq nat n (succ zero)))))) (car-atom $result))

; Open beta conversion with an application as the argument.
!(let $result (try (set:proves &thy
   (pf:fix n (pf:fix r (pf:assume k
     (pf:imp-elim (pf:all-elim k (succ n))
       (pf:all-elim (pf:known refl@nat) (succ n))))))
   (all nat (lam n (exists-nat (lam x (eq nat x (succ n)))))))) (car-atom $result))
!(let $result (try (set:proves &thy
   (pf:fix n (pf:fix r (pf:assume k
     (pf:imp-elim (pf:all-elim k (succ n))
       (pf:all-elim (pf:known refl@nat) (succ n))))))
   (all nat (lam n (exists-nat (lam x (eq nat x n))))))) (car-atom $result))
