(hol:profile base)

; A subset bubble: definitional proof irrelevance for one refinement family,
; carved out in one space.  `(hol:irrelevant F J)` is admitted only when J
; proves that any two inhabitants of every instance of F are identical; the
; published rule records J.  Inside, two proofs of one refinement are one,
; so the subset elements built from them are equal by `refl`.  Outside, the
; same identity needs J explicitly.  Different numbers stay different, and an
; unjustified declaration is not admitted.

!(bind! &sub (let $s (new-space) (let $_ (add-atom $s (hol:profile base)) $s)))
!(hol:inductive &sub nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(add-atom &sub (: Even (-> nat (u 0))))
!(add-atom &sub (: even-zero (Even zero)))
!(add-atom &sub (: even-zero' (Even zero)))
!(add-atom &sub (: even-two (Even (succ (succ zero)))))
!(add-atom &sub (: even-unique (-> (n : nat) (p : (Even n)) (q : (Even n)) (id (Even n) p q))))

; 1. Before the declaration: two proofs, two subset elements.
!(type:eq &sub even-zero even-zero')
!(type:check &sub (refl (pair zero even-zero))
   (id (sigma (k : nat) (Even k)) (pair zero even-zero) (pair zero even-zero')))
; 2. The same identity by explicit evidence.
!(type:check &sub
   (id:eliminate (Even zero) even-zero
     (lam y (lam e (id (sigma (k : nat) (Even k)) (pair zero even-zero) (pair zero y))))
     (refl (pair zero even-zero)) even-zero' (even-unique zero even-zero even-zero'))
   (id (sigma (k : nat) (Even k)) (pair zero even-zero) (pair zero even-zero')))

; 3. The bubble.
!(hol:irrelevant &sub Even even-unique)
!(type:eq &sub even-zero even-zero')
!(type:check &sub (refl (pair zero even-zero))
   (id (sigma (k : nat) (Even k)) (pair zero even-zero) (pair zero even-zero')))
; 4. Not degenerate.
!(type:check &sub (refl (pair zero even-zero))
   (id (sigma (k : nat) (Even k)) (pair zero even-zero) (pair (succ (succ zero)) even-two)))

; 5. Another space with the same declarations and no bubble.
!(bind! &out (let $s (new-space) (let $_ (add-atom $s (hol:profile base)) $s)))
!(hol:inductive &out nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(add-atom &out (: Even (-> nat (u 0))))
!(add-atom &out (: even-zero (Even zero)))
!(add-atom &out (: even-zero' (Even zero)))
!(type:eq &out even-zero even-zero')

; 6. An unjustified declaration is not admitted.
!(add-atom &sub (: Odd (-> nat (u 0))))
!(add-atom &sub (: odd-claim (-> (k : nat) (Odd k))))
!(hol:irrelevant &sub Odd odd-claim)
