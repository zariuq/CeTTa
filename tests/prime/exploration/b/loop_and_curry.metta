(set:profile hol)

; A recursor whose unfolding calls it at its own argument, and the unfolding
; without its premises.
;
; With the step loop := lam x z. z x, the definitional unfolding of
; rec R loop a returns to rec R loop a after one unfolding and three beta steps
; (loop_cycle).  In the candidate, whose recursive call takes the inverted
; accessibility proof, one unfolding and four beta steps return to the
; recursor at the same point, and no normal form is ever reached
; (Signature.loop_cycle, Signature.loop_no_normal_form).  So the unfolding
; is not admitted as a definition, and the looping call is named.  It is not
; refuted: a refusal says that no set satisfies the equation, and the draft
; has no such witness for a step that is any function.  With the unfolding
; propositional the recursor never unfolds, and the same conversion problem
; is decided by beta alone.
;
; Without the accessibility premise the unfolding proves falsum (Curry).  As a
; definition it is not admitted; as an assumption it proves falsum, and the
; proof is refused once the premises are required.

!(import! &self pf)
(= (verdict-of $r)
   (unify $r (Established $e) Established
     (unify $r (Undetermined $w) Undetermined
       (unify $r (Refuted $w) Refuted other))))
!(bind! &thy (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile hol)) $profile-space)))
!(pf:theory &thy ((: A (u 0)) (: Res (u 0))
                  (: rec (-> (-> A A prop) (-> A (-> A Res) Res) A Res))
                  (: r (-> A A prop)) (: a A)))

; 1. The definitional unfolding is not admitted, the looping call named
;    (loop_no_normal_form, Signature.loop_no_normal_form), and nothing is
;    published.
!(let $v (try (set:define &thy recd (-> (-> A A prop) (-> A (-> A Res) Res) A Res)
     (= (recd $R $F $x) ($F $x (lam b (recd $R $F b)))))) (verdict-of $v))
!(== (collapse (match &thy (: recd $type) $type)) ())

; 2. With the unfolding propositional, the loop's conversion problem is
;    decided by beta: the unfolding reduces back to the call
;    (loop_unfold_betaConv, loop_carved_decided).  In the candidate, whose
;    recursive call carries the inverted accessibility proof, the same problem
;    is decided negatively (Signature.loop_inert_not_conv).
!(set:eq &thy (rec r (lam (x : A) (lam (z : (-> A Res)) (z x))) a)
   ((lam (x : A) (lam (z : (-> A Res)) (z x))) a
    (lam (b : A) (rec r (lam (x : A) (lam (z : (-> A Res)) (z x))) b))))

; 3. Curry, in a theory whose results are propositions.
!(bind! &curry (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile hol)) $profile-space)))
!(pf:theory &curry ((: recu (-> (-> prop prop prop) (-> prop (-> prop prop) prop) prop prop))))
!(pf:equality &curry prop refl@prop subst@prop)
!(set:define &curry falsum prop (= falsum (all prop (lam p p))))
!(set:define &curry any-rel (-> prop prop prop) (= (any-rel $y $x) falsum))
!(set:define &curry curry-step (-> prop (-> prop prop) prop) (= (curry-step $x $z) (imp ($z $x) falsum)))
;    A code equal to its own implication of falsum is refuted as a
;    definition: no truth value is its own negation (CurryData.curry_falsum).
;    The unguarded unfolding is not admitted; at the Curry step no set
;    satisfies it (curry_unguarded), but the draft names no witness for a
;    step that is any function.
!(let $v (try (set:define &curry c prop (= c (imp c falsum)))) (verdict-of $v))
!(let $v (try (set:define &curry recd (-> (-> prop prop prop) (-> prop (-> prop prop) prop) prop prop)
     (= (recd $R $F $x) ($F $x (lam b (recd $R $F b)))))) (verdict-of $v))
;    As an assumption, the unguarded unfolding proves falsum: the code
;    recu any-rel curry-step falsum equals its own implication of falsum, and
;    transport along that equation both ways refutes it and proves it
;    (CurryData.curry_falsum, curry_unguarded).
!(set:axiom &curry unguarded
   (all (-> prop prop prop) (lam R (all (-> prop (-> prop prop) prop) (lam F (all prop (lam x
     (eq prop (recu R F x) (F x (lam b (recu R F b)))))))))))
!(pf:theorem &curry curry-eq
   (eq prop (recu any-rel curry-step falsum) (imp (recu any-rel curry-step falsum) falsum))
   (pf:all-elim (pf:all-elim (pf:all-elim (pf:known unguarded) any-rel) curry-step) falsum))
!(pf:theorem &curry curry-refute (imp (recu any-rel curry-step falsum) falsum)
   (pf:assume h (pf:imp-elim (pf:subst-with prop (lam z z) (recu any-rel curry-step falsum)
                               (imp (recu any-rel curry-step falsum) falsum) (pf:known curry-eq) h)
                  h)))
!(pf:theorem &curry curry-prove (recu any-rel curry-step falsum)
   (pf:imp-elim (pf:subst-with prop (lam z (imp z (recu any-rel curry-step falsum)))
                  (recu any-rel curry-step falsum) (imp (recu any-rel curry-step falsum) falsum)
                  (pf:known curry-eq) (pf:assume h h))
     (pf:known curry-refute)))
!(pf:theorem &curry curry falsum (pf:imp-elim (pf:known curry-refute) (pf:known curry-prove)))
;    With the premises the first step does not check: the equation is only
;    available under accessibility and respect, and no closed proof of falsum
;    exists in the guarded package (guard_is_essential,
;    PropInstance.guard_is_essential).
!(set:define &curry acc (-> (-> prop prop prop) prop prop)
   (= (acc $R $a) (all (-> prop prop) (lam X (imp (all prop (lam x (imp (all prop (lam y (imp ($R y x) (X y)))) (X x)))) (X $a))))))
!(set:define &curry resp (-> (-> prop prop prop) (-> prop (-> prop prop) prop) prop)
   (= (resp $R $F) (all prop (lam x (all (-> prop prop) (lam g (all (-> prop prop) (lam h
      (imp (all prop (lam y (imp ($R y x) (eq prop (g y) (h y))))) (eq prop ($F x g) ($F x h)))))))))))
!(set:axiom &curry guarded
   (all (-> prop prop prop) (lam R (all (-> prop (-> prop prop) prop) (lam F (all prop (lam x
     (imp (acc R x) (imp (resp R F) (eq prop (recu R F x) (F x (lam b (recu R F b)))))))))))))
!(pf:theorem &curry curry-eq-guarded
   (eq prop (recu any-rel curry-step falsum) (imp (recu any-rel curry-step falsum) falsum))
   (pf:all-elim (pf:all-elim (pf:all-elim (pf:known guarded) any-rel) curry-step) falsum))
