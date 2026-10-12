(hol:profile core)

; A bubble whose definitional equality includes the recursion equations of
; addition: a space where `add` is admitted with its equations.  There a
; vector of length n is, by conversion alone, a vector of length n + 0, and
; the bubble exports that equation as an identity proof, `refl`.  The same
; signature with `add` opaque has no such conversion: the vector moves to
; length n + 0 only by explicit transport along the exported proof.  Neither
; space identifies n with n + 1, and the recursion equations alone do not
; make 0 + n compute at a variable.

!(bind! &arith (let $s (new-space) (let $_ (add-atom $s (hol:profile core)) $s)))
!(hol:inductive &arith nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(hol:define &arith add (-> nat nat nat)
   (= (add $n zero) $n)
   (= (add $n (succ $m)) (succ (add $n $m))))
!(add-atom &arith (: Vec (-> nat (u 0))))
!(add-atom &arith (: n nat))
!(add-atom &arith (: v (Vec n)))

; 1. Inside: definitional.
!(type:check &arith v (Vec (add n zero)))
; 2. Inside, not degenerate, and 0 + n is not a computation of the equations.
!(type:check &arith v (Vec (succ n)))
!(type:check &arith v (Vec (add zero n)))
; 3. The bubble's equation as an exported proof.
!(type:check &arith (lam k (refl k)) (-> (k : nat) (id nat k (add k zero))))

!(bind! &plain (let $s (new-space) (let $_ (add-atom $s (hol:profile core)) $s)))
!(hol:inductive &plain nat (u 0) (: zero nat) (: succ (-> nat nat)))
!(add-atom &plain (: add (-> nat nat nat)))
!(add-atom &plain (: add-zero (-> (k : nat) (id nat k (add k zero)))))
!(add-atom &plain (: Vec (-> nat (u 0))))
!(add-atom &plain (: n nat))
!(add-atom &plain (: v (Vec n)))

; 4. Outside: no conversion.
!(type:check &plain v (Vec (add n zero)))
; 5. Outside: explicit transport along the exported proof, at the right length only.
!(type:check &plain
   (id:eliminate nat n (lam k (lam e (Vec k))) v (add n zero) (add-zero n))
   (Vec (add n zero)))
!(type:check &plain
   (id:eliminate nat n (lam k (lam e (Vec k))) v (add n zero) (add-zero n))
   (Vec n))
