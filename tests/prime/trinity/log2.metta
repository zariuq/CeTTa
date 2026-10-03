; The base-two logarithm, a recursion that is not structural, on three faces:
;   log2 0 = 0,  log2 1 = 0,  log2 (n + 2) = 1 + log2 (half (n + 2)),
;   half 0 = 0,  half 1 = 0,  half (n + 2) = 1 + half n.
; log2 calls itself at half (n + 2), which is not a part of n + 2, and half
; calls itself two constructors down, so the check for recursion on an
; immediate part leaves both Undetermined.  Each is admitted instead by a
; bound on its
; calls, (by bound num B proof ...): B maps the arguments to num, and the
; proofs show that at every call the bound goes down, so every chain of
; calls from x is at most B x long.  The equations are admitted as written
; and run as written.
!(import! &self pf)
!(set:inductive &self num (u 0) (: zero num) (: suc (-> num num)))
!(set:known-proposition &self num-ind)
!(pf:equality &self num refl@num subst@num)

; ---- What the structural check says.
; Without a clause it does not read the nested patterns (three equations for
; two constructors), and the verdict names the route that does.
!(try (set:define &self log2-plain (-> num num)
   (= (log2-plain zero) zero)
   (= (log2-plain (suc zero)) zero)
   (= (log2-plain (suc (suc $n))) (suc (log2-plain (half-plain (suc (suc $n))))))))

; ---- The bound: "below" is the least relation in which n is below n + 1
; and what is below n is below n + 1; the obligations of a bound are written
; with it.
!(set:define &self below (-> num num prop)
   (= (below $m $n)
      (all (-> num num prop) (lam R
        (imp (all num (lam x (R x (suc x))))
             (imp (all num (lam l (all num (lam x (imp (R l x) (R l (suc x)))))))
                  (R $m $n)))))))
!(pf:theorem &self below-two (all num (lam n (below n (suc (suc n)))))
   (pf:fix n (pf:fix R (pf:assume base (pf:assume step
     (pf:imp-elim (pf:all-elim (pf:all-elim step n) (suc n)) (pf:all-elim base n)))))))
; half is admitted with the bound n: its call at n is below n + 2.
!(set:define &self half (-> num num) (by bound num (lam n n) (pf:known below-two))
   (= (half zero) zero)
   (= (half (suc zero)) zero)
   (= (half (suc (suc $n))) (suc (half $n))))
; Two facts about below, and that half n is below n + 1, for n and n + 1
; together, by induction.
!(pf:theorem &self below-later (all num (lam a (all num (lam b (imp (below a b) (below a (suc b)))))))
   (pf:fix a (pf:fix b (pf:assume h (pf:fix R (pf:assume base (pf:assume step
     (pf:imp-elim (pf:all-elim (pf:all-elim step a) b)
       (pf:imp-elim (pf:imp-elim (pf:all-elim h R) base) step)))))))))
!(pf:theorem &self below-suc (all num (lam a (all num (lam b (imp (below a b) (below (suc a) (suc b)))))))
   (pf:fix a (pf:fix b (pf:assume h (pf:fix R (pf:assume base (pf:assume step
     (pf:imp-elim (pf:imp-elim (pf:all-elim h (lam x (lam y (R (suc x) (suc y)))))
                    (pf:fix x (pf:all-elim base (suc x))))
       (pf:fix l (pf:fix x (pf:all-elim (pf:all-elim step (suc l)) (suc x))))))))))))
!(set:define &self both (-> prop prop prop)
   (= (both $p $q) (all prop (lam r (imp (imp $p (imp $q r)) r)))))
!(pf:theorem &self half-below-both
   (all num (lam n (both (below (half n) (suc n)) (below (half (suc n)) (suc (suc n))))))
   (pf:induction num-ind
     (lam n (both (below (half n) (suc n)) (below (half (suc n)) (suc (suc n)))))
     (pf:fix r (pf:assume k
       (pf:imp-elim (pf:imp-elim k (pf:fix R (pf:assume base (pf:assume step (pf:all-elim base zero)))))
         (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known below-later) zero) (suc zero))
           (pf:fix R (pf:assume base (pf:assume step (pf:all-elim base zero))))))))
     (pf:fix m (pf:assume ih
       (pf:fix r (pf:assume k
         (pf:imp-elim (pf:all-elim ih r)
           (pf:assume first (pf:assume second
             (pf:imp-elim (pf:imp-elim k second)
               (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known below-later) (suc (half m))) (suc (suc m)))
                 (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known below-suc) (half m)) (suc m)) first))))))))))))
; So the call of log2 at half (n + 2) is below n + 2.
!(pf:theorem &self log2-calls-go-down
   (all num (lam n (below (half (suc (suc n))) (suc (suc n)))))
   (pf:fix n
     (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known below-suc) (half n)) (suc n))
       (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known half-below-both) n) (below (half n) (suc n)))
         (pf:assume first (pf:assume second first))))))
; log2 is admitted with the bound n.
!(set:define &self log2 (-> num num) (by bound num (lam n n) (pf:known log2-calls-go-down))
   (= (log2 zero) zero)
   (= (log2 (suc zero)) zero)
   (= (log2 (suc (suc $n))) (suc (log2 (half (suc (suc $n)))))))
; The record keeps the bound as the termination evidence, beside the
; equations: the type of the bound, the bound, and its obligation.
!(let (definition-evidence $rules $termination $lean) (set:known-proof &self log2)
   (let (bound $type $measure $obligations) $termination
     (Bound $type $measure (size-atom $obligations))))
; It also names the Lean theorem the admission rests on, a set model for a
; definition whose calls go down a bound, and the lemmas that give its
; hypotheses from the checks: the structural order of num is well-founded,
; and linear constructor patterns that do not overlap are apart.
!(let (definition-evidence $rules $termination $lean) (set:known-proof &self log2)
   (quote $lean))

; ---- Operational: log2 8 runs to 3, by the equations as written.
!(log2 (suc (suc (suc (suc (suc (suc (suc (suc zero)))))))))
; Its firings are those of the written equations: four of log2, at 8,
; 4, 2 and 1, and ten of half, at 8, 6, 4, 2, 0, then 4, 2, 0, then 2, 0.
; The count is printed with the term it counts.
!(let ($values $n) (cost:firings (log2 (suc (suc (suc (suc (suc (suc (suc (suc zero))))))))))
   (quote (firings-of (log2 (suc (suc (suc (suc (suc (suc (suc (suc zero))))))))) $values $n)))
!(log2 (suc (suc zero)))
!(log2 (suc (suc (suc zero))))

; ---- Intensional: typed, and equal by computation at closed numbers.
!(type:of &self log2)
!(type:eq &self (log2 (suc (suc (suc (suc (suc (suc (suc (suc zero))))))))) (suc (suc (suc zero))))
; log2 2 and log2 3 are one number.
!(type:eq &self (log2 (suc (suc zero))) (log2 (suc (suc (suc zero)))))
; log2 4 is 2, not 1.
!(type:eq &self (log2 (suc (suc (suc (suc zero))))) (suc zero))
; The refutation assumes that conversion is complete for the package, which
; Lean proves for the bare tower and the object package; here the comparison
; meets a declared datatype and definitions admitted by a bound, and the
; reason says so.
!(try (type:eq &self (log2 (suc (suc (suc (suc zero))))) (suc zero)))
; The written equations hold at typed instances, the third at every n by
; one step of computation.
!(pf:theorem &self log2-step
   (all num (lam n (eq num (log2 (suc (suc n))) (suc (log2 (half (suc (suc n))))))))
   (pf:fix n (pf:refl num (log2 (suc (suc n))))))
!(pf:proves &self (pf:by log2-step)
   (eq num (log2 (suc (suc (suc zero)))) (suc (log2 (half (suc (suc (suc zero))))))))

; ---- Extensional: the value is the set the equations name.
!(set:eq &self (log2 (suc (suc (suc (suc (suc (suc (suc (suc zero))))))))) (suc (suc (suc zero))))
!(set:eq &self (log2 (suc (suc (suc (suc (suc (suc (suc (suc zero))))))))) (suc (suc zero)))

; ---- log2 admitted a second way, on evidence that its equations have a
; solution in sets: log2 above is one, and the equations hold of it by
; computation (the third at every n).  The two run to the same values; the
; second unfolds only at closed arguments, so at an unknown number its third
; equation is Undetermined, while the first's is Established.  What that
; line shows: one step of the first's third equation joins the two sides,
; and the step is an instance of an admitted equation, so the equation holds
; (in Lean, typed instances of a package's equations are equalities of the
; judgment).  What it does not show: that unfolding the bound-admitted rules
; stops at open terms; Lean does not state that, and the checker's search is
; bounded by its budget.  The Established in a `try` result is `try`'s
; success constructor around the conversion verdict, not a stored proof.
!(set:define &self log2-solved (-> num num)
   (by set-solution
     (pf:fix r (pf:assume k
       (pf:imp-elim (pf:all-elim k log2)
         (pf:fix c (pf:assume j
           (pf:imp-elim (pf:imp-elim j (pf:all-elim (pf:known refl@num) zero))
             (pf:fix c2 (pf:assume j2
               (pf:imp-elim (pf:imp-elim j2 (pf:all-elim (pf:known refl@num) zero))
                 (pf:fix n (pf:all-elim (pf:known refl@num) (log2 (suc (suc n)))))))))))))))
   (= (log2-solved zero) zero)
   (= (log2-solved (suc zero)) zero)
   (= (log2-solved (suc (suc $n))) (suc (log2-solved (half (suc (suc $n)))))))
!(log2-solved (suc (suc (suc (suc (suc (suc (suc (suc zero)))))))))
!(let ($values $n) (cost:firings (log2-solved (suc (suc (suc (suc (suc (suc (suc (suc zero))))))))))
   (quote (firings-of (log2-solved (suc (suc (suc (suc (suc (suc (suc (suc zero))))))))) $values $n)))
!(type:eq &self (log2-solved (suc (suc (suc zero)))) (log2 (suc (suc (suc zero)))))
!(try (type:eq &self (lam (n : num) (log2-solved (suc (suc n))))
                     (lam (n : num) (suc (log2-solved (half (suc (suc n))))))))
!(try (type:eq &self (lam (n : num) (log2 (suc (suc n))))
                     (lam (n : num) (suc (log2 (half (suc (suc n))))))))
!(let (definition-evidence $rules $termination $lean) (set:known-proof &self log2-solved)
   (car-atom $termination))
; Its record names the Lean theorem for admission on evidence of a solution.
!(let (definition-evidence $rules $termination $lean) (set:known-proof &self log2-solved)
   (quote $lean))

; ---- Negative controls.
; A bound that is too small: the constant zero.  Its obligation says that
; zero is below zero, and the proof given is of another statement: the
; definition is Undetermined, with the obligation as the reason.
!(let $r (try (set:define &self log2-short (-> num num) (by bound num (lam n zero) (pf:known log2-calls-go-down))
   (= (log2-short zero) zero)
   (= (log2-short (suc zero)) zero)
   (= (log2-short (suc (suc $n))) (suc (log2-short (half (suc (suc $n))))))))
   (let $reason (index-atom $r 1) ((car-atom $r) (car-atom $reason))))
; A bound without its proofs: its obligation is stated in the verdict, the
; call of log2 at half (n + 2) below n + 2, with below written out.
!(let $r (try (set:define &self log2-unproved (-> num num) (by bound num (lam n n))
   (= (log2-unproved zero) zero)
   (= (log2-unproved (suc zero)) zero)
   (= (log2-unproved (suc (suc $n))) (suc (log2-unproved (half (suc (suc $n))))))))
   (let $reason (index-atom $r 1) (let $obligations (index-atom $reason 2)
     ((car-atom $r) (car-atom $reason) (quote $obligations)))))
; The obligations speak of calls only, so for every other occurrence of the
; constant the clause leaves the definition Undetermined, naming the
; occurrence: a call inside a call's argument, the constant passed
; unapplied, and a call under a binder, whose variable the obligations cannot
; see.
!(set:define &self twice (-> (-> num num) num num) (= (twice $g $x) ($g ($g $x))))
!(let $r (try (set:define &self nest (-> num num) (by bound num (lam n n) (pf:known below-two))
   (= (nest zero) zero) (= (nest (suc $n)) (nest (nest $n)))))
   (let $reason (index-atom $r 1) ((car-atom $r) (index-atom $reason 1))))
!(let $r (try (set:define &self pass (-> num num) (by bound num (lam n n) (pf:known below-two))
   (= (pass zero) zero) (= (pass (suc $n)) (twice pass $n))))
   (let $reason (index-atom $r 1) ((car-atom $r) (index-atom $reason 1))))
!(let $r (try (set:define &self under (-> num num) (by bound num (lam n n) (pf:known below-two))
   (= (under zero) zero) (= (under (suc $n)) (suc ((lam k (under k)) (suc $n))))))
   (let $reason (index-atom $r 1) ((car-atom $r) (index-atom $reason 1))))
!(let $r (try (set:define &self under-let (-> num num) (by bound num (lam n n) (pf:known below-two))
   (= (under-let zero) zero) (= (under-let (suc $n)) (let $m $n (under-let $m)))))
   (let $reason (index-atom $r 1) ((car-atom $r) (index-atom $reason 1))))
; The third equation without its successor.  Its call is the same, so the
; same bound admits it, on its own merits: it is the function zero, and at 2
; it gives 0 where log2 gives 1.
!(set:define &self log2-altered (-> num num) (by bound num (lam n n) (pf:known log2-calls-go-down))
   (= (log2-altered zero) zero)
   (= (log2-altered (suc zero)) zero)
   (= (log2-altered (suc (suc $n))) (log2-altered (half (suc (suc $n))))))
!(log2-altered (suc (suc zero)))
!(type:eq &self (log2-altered (suc (suc zero))) (log2 (suc (suc zero))))
