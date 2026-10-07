(set:profile hol)

; The accessibility predicate as a proposition code, defined impredicatively
; over a carrier A:
;   acc R a := all X. (all x. (all y. R y x -> X y) -> X x) -> X a
; Its introduction and inversion are checked theorems: no new constant and no
; new rule.  The empty relation is well founded, and a reflexive relation has
; no accessible point.  A well-founded recursor with no computation rule is
; then used through its unfolding, with the accessibility premise proved.

!(import! &self pf)
!(bind! &thy (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile hol)) $profile-space)))
!(pf:theory &thy ((: A (u 0)) (: Res (u 0))
                  (: rec (-> (-> A A prop) (-> A (-> A Res) Res) A Res))
                  (: a A) (: c Res) (: Q (-> Res prop))))
!(pf:equality &thy Res refl@Res subst@Res)
!(set:define &thy falsum prop (= falsum (all prop (lam p p))))
!(set:define &thy top prop (= top (imp falsum falsum)))
!(set:define &thy none (-> A A prop) (= (none $y $x) falsum))
!(set:define &thy every (-> A A prop) (= (every $y $x) top))

; 1. The code (CodeNames.acc; read in the consistency model it is AccCode,
;    which is Acc by accCode_iff_acc).
!(set:define &thy acc (-> (-> A A prop) A prop)
   (= (acc $R $a) (all (-> A prop) (lam X (imp (all A (lam x (imp (all A (lam y (imp ($R y x) (X y)))) (X x)))) (X $a))))))

; 2. Introduction (CodeNames.Laws.intro_typed, AccCode.intro): a point whose
;    predecessors are accessible is accessible.
!(pf:theorem &thy acc-intro
   (all (-> A A prop) (lam R (all A (lam a (imp (all A (lam y (imp (R y a) (acc R y)))) (acc R a))))))
   (pf:fix R (pf:fix a (pf:assume h (pf:fix X (pf:assume s
      (pf:imp-elim (pf:all-elim s a)
        (pf:fix y (pf:assume r (pf:imp-elim (pf:all-elim (pf:imp-elim (pf:all-elim h y) r) X) s))))))))))

; 3. Inversion (CodeNames.Laws.inv_typed, AccCode.inv): the impredicative
;    quantifier at the predicate "every predecessor is accessible", which is
;    progressive by the introduction.
!(pf:theorem &thy acc-inv
   (all (-> A A prop) (lam R (all A (lam a (imp (acc R a) (all A (lam y (imp (R y a) (acc R y)))))))))
   (pf:fix R (pf:fix a (pf:assume q
     (pf:imp-elim (pf:all-elim q (lam z (all A (lam y (imp (R y z) (acc R y))))))
       (pf:fix x (pf:assume i (pf:fix y (pf:assume r
         (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known acc-intro) R) y)
           (pf:imp-elim (pf:all-elim i y) r)))))))))))

; 4. Controls.  The empty relation is well founded (accCode_of_wellFounded).
;    A reflexive relation has no accessible point (not_accCode_of_refl), so
;    the loop's hypotheses are unsatisfiable (loopHyps_unsatisfiable).  The
;    introduction's statement is not proved by its premise alone.
!(pf:theorem &thy acc-none (all A (lam a (acc none a)))
   (pf:fix a (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known acc-intro) none) a)
     (pf:fix y (pf:assume r (pf:all-elim r (acc none y)))))))
!(pf:theorem &thy acc-every-false (all A (lam a (imp (acc every a) falsum)))
   (pf:fix a (pf:assume q
     (pf:imp-elim (pf:all-elim q (lam z falsum))
       (pf:fix x (pf:assume i (pf:imp-elim (pf:all-elim i x) (pf:assume f f))))))))
!(pf:theorem &thy acc-intro-by-premise
   (all (-> A A prop) (lam R (all A (lam a (imp (all A (lam y (imp (R y a) (acc R y)))) (acc R a))))))
   (pf:fix R (pf:fix a (pf:assume h h))))

; 5. The recursor has no computation rule.  Its unfolding is an axiom under
;    the accessibility premise and the step's respect for the relation, as in
;    the propositional unfolding rule (unfoldEqual; the candidate's
;    Signature.unfoldType, whose guard is the accessibility proof itself).
!(set:define &thy resp (-> (-> A A prop) (-> A (-> A Res) Res) prop)
   (= (resp $R $F) (all A (lam x (all (-> A Res) (lam g (all (-> A Res) (lam h
      (imp (all A (lam y (imp ($R y x) (eq Res (g y) (h y))))) (eq Res ($F x g) ($F x h)))))))))))
!(set:axiom &thy unfold-eq
   (all (-> A A prop) (lam R (all (-> A (-> A Res) Res) (lam F (all A (lam x
     (imp (acc R x) (imp (resp R F) (eq Res (rec R F x) (F x (lam b (rec R F b)))))))))))))

; 6. The premises proved, for the constant step over the empty relation: the
;    unfolding holds with no assumption beyond the axiom.
!(set:define &thy const-c (-> A (-> A Res) Res) (= (const-c $x $g) c))
!(pf:theorem &thy resp-const (all (-> A A prop) (lam R (resp R const-c)))
   (pf:fix R (pf:fix x (pf:fix g (pf:fix h (pf:assume e (pf:refl Res c)))))))
!(pf:theorem &thy unfold-const
   (eq Res (rec none const-c a) (const-c a (lam (b : A) (rec none const-c b))))
   (pf:imp-elim (pf:imp-elim
      (pf:all-elim (pf:all-elim (pf:all-elim (pf:known unfold-eq) none) const-c) a)
      (pf:all-elim (pf:known acc-none) a))
    (pf:all-elim (pf:known resp-const) none)))

; 7. Transport along the unfolding proves a statement about the unfolded call
;    (Signature.transport_goal, positive_carved) ...
!(set:axiom &thy h-q (Q (rec none const-c a)))
!(pf:theorem &thy goal (Q c)
   (pf:subst-with Res (lam z (Q z)) (rec none const-c a) (const-c a (lam (b : A) (rec none const-c b)))
     (pf:known unfold-const) (pf:known h-q)))
; ... which conversion alone does not give: the call is not convertible to its
;    unfolding (PropInstance.separation_constant_step, positive_carved_not_conv;
;    const-c computes to c as lam x g. c does), although conversion is
;    reflexive on it ...
!(set:eq &thy (rec none const-c a) (const-c a (lam (b : A) (rec none const-c b))))
!(set:eq &thy (rec none const-c a) (rec none const-c a))
; ... so the goal does not follow from the hypothesis by conversion
;    (PropInstance.hypothesis_not_conv_goal_constant_step,
;    positive_needs_witness).
!(pf:theorem &thy goal-by-conversion (Q c) (pf:known h-q))
