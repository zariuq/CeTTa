; One definition, an execution, and proofs for every natural number.
(hol:profile base)
!(bind! &math (let $s (new-space) (let $_ (add-atom $s (hol:profile base)) $s)))
!(hol:inductive &math nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(hol:define &math add (-> nat nat nat)
   (= (add $n zero) $n)
   (= (add $n (succ $m)) (succ (add $n $m))))

; Run the same admitted definition: 2 + 1 = 3.
!(eval (add (succ (succ zero)) (succ zero)) (admitted &math))

; For every n, n + 0 = n follows from the defining equation.
!(type:theorem &math add-zero
   (-> (n : nat) (id nat (add n zero) n))
   (lam n (refl n)))
!(type:check &math add-zero (-> (n : nat) (id nat (add n zero) n)))

; For every n, 0 + n = n needs induction. The zero case is reflexivity;
; the successor case transports the induction hypothesis through succ.
!(type:theorem &math zero-add
   (-> (n : nat) (id nat (add zero n) n))
   (lam n
     (nat-rec (lam k (id nat (add zero k) k))
       (refl zero)
       (lam k (lam ih
         (id:eliminate nat (add zero k)
           (lam y (lam e (id nat (succ (add zero k)) (succ y))))
           (refl (succ (add zero k))) k ih)))
       n)))
!(type:check &math zero-add (-> (n : nat) (id nat (add zero n) n)))

; Reuse the checked universal proof at a concrete input.
!(type:check &math (zero-add (succ (succ zero)))
   (id nat (add zero (succ (succ zero))) (succ (succ zero))))

; Reject a proof with the wrong endpoint: 2 + 1 is not 2.
!(type:check &math (refl (succ (succ zero)))
   (id nat (add (succ (succ zero)) (succ zero)) (succ (succ zero))))
; The same proof cannot establish the false universal claim n + 0 = succ n.
!(type:check &math add-zero
   (-> (n : nat) (id nat (add n zero) (succ n))))
