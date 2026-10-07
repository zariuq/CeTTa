(set:profile hol)

; Propositional unfolding: an unfolding equation that is a theorem and not a
; definitional equality.  rec stands for well-founded recursion with no
; computation rule; its unfolding is an axiom whose premises are the
; accessibility of the point (acc) and the step's respect for the relation
; (resp).  In the carved theory the unfolding is provable propositionally
; and used by transport, while conversion never performs it.

!(import! &self pf)
!(bind! &thy (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile hol)) $profile-space)))
!(pf:theory &thy ((: A (u 0)) (: Res (u 0))
                  (: rec (-> (-> A A prop) (-> A (-> A Res) Res) A Res))
                  (: acc (-> (-> A A prop) A prop))
                  (: resp (-> (-> A A prop) (-> A (-> A Res) Res) prop))
                  (: r (-> A A prop)) (: s (-> A (-> A Res) Res)) (: a A)
                  (: Q (-> Res prop))))
!(pf:equality &thy Res refl@Res subst@Res)
; the unfolding, propositionally: acc R x -> resp R F -> rec R F x = F x (rec R F)
!(set:axiom &thy unfold-eq
   (all (-> A A prop) (lam R (all (-> A (-> A Res) Res) (lam F (all A (lam x
     (imp (acc R x) (imp (resp R F)
       (eq Res (rec R F x) (F x (lam b (rec R F b)))))))))))))
; the positive control's hypotheses: Q (rec r s a), acc r a, resp r s
!(set:axiom &thy h-q (Q (rec r s a)))
!(set:axiom &thy h-acc (acc r a))
!(set:axiom &thy h-resp (resp r s))

; 1. The unfolding equation at a is a theorem (positive_carved_equal).
!(pf:theorem &thy unfold-at-a (eq Res (rec r s a) (s a (lam b (rec r s b))))
   (pf:imp-elim (pf:imp-elim (pf:all-elim (pf:all-elim (pf:all-elim (pf:known unfold-eq) r) s) a)
                             (pf:known h-acc))
                (pf:known h-resp)))
; 2. Transport along it proves the goal about the unfolded call (positive_carved).
!(pf:theorem &thy goal (Q (s a (lam b (rec r s b))))
   (pf:subst-with Res (lam z (Q z)) (rec r s a) (s a (lam b (rec r s b)))
                  (pf:known unfold-at-a) (pf:known h-q)))
; 3. Conversion does not relate the call with its unfolding
;    (positive_carved_not_conv) ...
!(set:eq &thy (rec r s a) (s a (lam b (rec r s b))))
; ... though it is reflexive on the call itself,
!(set:eq &thy (rec r s a) (rec r s a))
; 4. ... so the goal does not follow from Q (rec r s a) by conversion alone;
;    the equation is used, not decorative (positive_needs_witness).
!(pf:theorem &thy goal-by-conversion (Q (s a (lam b (rec r s b)))) (pf:known h-q))
