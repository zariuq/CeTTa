(set:profile hol)

; The numbers from n, a process that never ends, on three faces:
;   from n = scons n (from (suc n))
; read as a stream, a function from positions to numbers: scons a s puts a at
; position zero and s one position later.  Run as a rule the equation never
; stops, and the check for structural recursion does not admit it.  It is
; admitted on evidence that some set satisfies it: a set: proof that a
; function of its type satisfies the equation at every position, here
; n, k |-> n + k.  Admitted this way, its rule waits for a closed position
; before it unfolds: an observation of a position stops, and the stream
; itself is never unfolded in an open term.
!(import! &self pf)
!(set:inductive &self num (u 0) (: zero num) (: suc (-> num num)))
!(set:known-proposition &self num-ind)
!(pf:equality &self num refl@num subst@num)
!(set:define &self add (-> num num num)
   (= (add $n zero) $n)
   (= (add $n (suc $k)) (suc (add $n $k))))
!(set:define &self scons (-> num (-> num num) num num)
   (= (scons $a $s zero) $a)
   (= (scons $a $s (suc $k)) ($s $k)))

; ---- Without evidence.
; As a definition by recursion the equation is not admitted: its call
; repeats its left side and never stops, yet some set may satisfy it.
!(try (set:define &self from-plain (-> num num num)
   (= (from-plain $n) (scons $n (from-plain (suc $n))))))
; The clause with no proof states what its evidence must prove: some
; function satisfies the equation at every n and every position.
!(let $r (try (set:define &self from-unproved (-> num num num) (by set-solution)
   (= (from-unproved $n) (scons $n (from-unproved (suc $n))))))
   (let $reason (index-atom $r 1) (let (quote $statement) (lift (index-atom $reason 1))
     ((car-atom $r) (car-atom $reason) (quote $statement)))))

; ---- The evidence: n, k |-> n + k satisfies the equation at every position.
!(pf:theorem &self add-suc-left
   (all num (lam n (all num (lam k (eq num (add (suc n) k) (suc (add n k)))))))
   (pf:fix n (pf:induction num-ind (lam k (eq num (add (suc n) k) (suc (add n k))))
     (pf:refl num (suc n))
     (pf:fix k (pf:assume ih
       (pf:cong num num (lam x (suc x)) (add (suc n) k) (suc (add n k)) ih))))))
!(pf:theorem &self from-solution-step
   (all num (lam n (all num (lam k
     (eq num (add n k) (scons n (lam j (add (suc n) j)) k))))))
   (pf:fix n (pf:induction num-ind
     (lam k (eq num (add n k) (scons n (lam j (add (suc n) j)) k)))
     (pf:refl num n)
     (pf:fix k (pf:assume ih
       (pf:sym num (add (suc n) k) (suc (add n k))
         (pf:all-elim (pf:all-elim (pf:known add-suc-left) n) k)))))))
!(set:define &self from (-> num num num)
   (by set-solution
     (pf:fix r (pf:assume h
       (pf:imp-elim (pf:all-elim h (lam n (lam k (add n k))))
         (pf:known from-solution-step)))))
   (= (from $n) (scons $n (from (suc $n)))))
; The record keeps the evidence as the termination evidence, beside the
; equation.  The type (-> num num num) is the type of streams of numbers
; from a number: num -> (num -> num).
!(let (definition-evidence $rules $termination $lean) (set:known-proof &self from)
   (car-atom $termination))
; It names the Lean theorem it rests on, admission on evidence of a solution,
; and the one that makes evidence given position by position enough for a
; function result.
!(let (quote (definition-evidence $rules $termination $lean)) (lift (set:known-proof &self from))
   (quote $lean))

; ---- Operational: the observation of position 2 of the stream from 1 runs
; to 1 + 2 = 3.  Its firings are those of the two written equations:
; from at positions 2, 1 and 0, and scons at the same three.  The count is
; printed with the term it counts.
!(from (suc zero) (suc (suc zero)))
!(let (quote ($values $n)) (lift (cost:firings (from (suc zero) (suc (suc zero)))))
   (quote (firings-of (from (suc zero) (suc (suc zero))) $values $n)))

; The stream from 1 is a value: from applied to its first argument only, a
; partial application, which stays as written.  Applied to a position, the
; call continues: the observation at position 2 is 3.  A call with more
; arguments than from takes is still an error.
!(from (suc zero))
!((from (suc zero)) (suc (suc zero)))
!(from (suc zero) (suc (suc zero)) zero)

; ---- Intensional: typed, an observation equal by computation at a closed
; position, also from an unknown number; at an unknown position, or with no
; position, nothing unfolds, and the comparison is not refuted.
!(type:of &self from)
!(type:eq &self (from (suc zero) (suc (suc zero))) (suc (suc (suc zero))))
!(add-atom &self (: m num))
!(type:eq &self (from m (suc zero)) (suc m))
!(try (type:eq &self (from m (suc zero)) m))
!(add-atom &self (: p num))
!(try (type:eq &self (from m p) (add m p)))
!(try (type:eq &self (from m) (scons m (from (suc m)))))
; The written equation holds as a theorem at typed instances, at a closed
; position by computation.
!(pf:theorem &self from-step-at-two
   (all num (lam n (eq num (from n (suc (suc zero))) (scons n (from (suc n)) (suc (suc zero))))))
   (pf:fix n (pf:refl num (suc (suc n)))))

; ---- Extensional: an observation is the number it names.
!(set:eq &self (from (suc zero) (suc (suc zero))) (suc (suc (suc zero))))

; ---- Negative controls.
; At the type of lists the same equation has no solution: the list from n
; would contain the list from (suc n) as a part, and so on.
!(set:inductive &self list (u 0) (: nil list) (: cons (-> num list list)))
!(try (set:define &self from-list (-> num list) (= (from-list $n) (cons $n (from-list (suc $n))))))
; Evidence for another equation does not admit this one: the proof that
; n, k |-> n + k solves from is offered for the constant stream
; same n = scons n (same n).  The evidence is not accepted, and the
; definition is Undetermined, not refuted.
!(let $r (try (set:define &self same (-> num num num)
   (by set-solution
     (pf:fix r (pf:assume h
       (pf:imp-elim (pf:all-elim h (lam n (lam k (add n k))))
         (pf:known from-solution-step)))))
   (= (same $n) (scons $n (same $n)))))
   (let $reason (index-atom $r 1) ((car-atom $r) (car-atom $reason))))
