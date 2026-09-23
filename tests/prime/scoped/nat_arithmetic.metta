; The Natural Number Game, as in the Lean ladder: addition recurses on its
; second argument, so n + 0 = n holds by computation; 0 + n and (n+1) + m
; need induction; commutativity and associativity follow.
!(import! &self pf)
!(bind! &thy (new-space))
!(set:inductive &thy nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(pf:equality &thy nat refl@nat subst@nat)
!(set:known-proposition &thy nat-ind)
!(set:define &thy add (-> nat nat nat)
   (= (add $n zero) $n)
   (= (add $n (succ $m)) (succ (add $n $m))))

; add_zero : n + 0 = n, by computation
!(pf:theorem &thy add-zero (all nat (lam n (eq nat (add n zero) n)))
   (pf:fix n (pf:refl nat n)))
; zero_add : 0 + n = n, by induction:  succ k => rw [add_succ, ih]
!(pf:theorem &thy zero-add (all nat (lam n (eq nat (add zero n) n)))
   (pf:induction nat-ind (lam n (eq nat (add zero n) n))
     (pf:refl nat zero)
     (pf:fix k (pf:assume ih (pf:cong nat nat (lam r (succ r)) (add zero k) k ih)))))
; succ_add : (n+1) + m = (n+m) + 1, by induction on m
!(pf:theorem &thy succ-add (all nat (lam n (all nat (lam m (eq nat (add (succ n) m) (succ (add n m)))))))
   (pf:fix n (pf:induction nat-ind (lam m (eq nat (add (succ n) m) (succ (add n m))))
     (pf:refl nat (succ n))
     (pf:fix k (pf:assume ih (pf:cong nat nat (lam r (succ r)) (add (succ n) k) (succ (add n k)) ih))))))
; add_comm : n + m = m + n, by induction on m:  succ k => rw [add_succ, ih, succ_add]
!(pf:theorem &thy add-comm (all nat (lam n (all nat (lam m (eq nat (add n m) (add m n))))))
   (pf:fix n (pf:induction nat-ind (lam m (eq nat (add n m) (add m n)))
     (pf:sym nat (add zero n) n (pf:by zero-add))
     (pf:fix k (pf:assume ih
       (pf:calc nat (add n (succ k))
         (= (succ (add k n))   (cong nat (lam r (succ r)) (add n k) (add k n) ih))
         (= (add (succ k) n)   (sym (by succ-add)))))))))
; add_assoc : (n + m) + p = n + (m + p), by induction on p
!(pf:theorem &thy add-assoc (all nat (lam n (all nat (lam m (all nat (lam p (eq nat (add (add n m) p) (add n (add m p)))))))))
   (pf:fix n (pf:fix m (pf:induction nat-ind (lam p (eq nat (add (add n m) p) (add n (add m p))))
     (pf:refl nat (add n m))
     (pf:fix k (pf:assume ih (pf:cong nat nat (lam r (succ r)) (add (add n m) k) (add n (add m k)) ih)))))))

; 2 + 1 = 1 + 2 by computation, and a wrong sum refuted.
!(pf:proves &thy (pf:refl nat (succ (succ (succ zero)))) (eq nat (add (succ (succ zero)) (succ zero)) (add (succ zero) (succ (succ zero)))))
!(pf:proves &thy (pf:refl nat (succ (succ zero))) (eq nat (add (succ (succ zero)) (succ zero)) (succ (succ zero))))
; commutativity used at symbolic numbers, instantiated by matching
!(pf:theory &thy ((: a nat) (: b nat)))
!(set:proves &thy (pf:by add-comm) (eq nat (add a b) (add b a)))
!(set:proves &thy (pf:by add-comm) (eq nat (add a b) (add a b)))
