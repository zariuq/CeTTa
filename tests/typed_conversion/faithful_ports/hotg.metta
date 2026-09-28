; Dedukti HOTG judgments with their free constants generalized and hypotheses discharged.
; Symmetry uses axSym : In(a,b) -> In(b,a) and axMem : In(a,b).
!(import! &self pf)
!(set:theorem hotg-mem (all set (lam a (all set (lam b (imp (In a b) (In a b)))))) (pf:all-intro (pf:all-intro (pf:imp-intro (pf:hyp 0)))))
!(set:theorem hotg-sym-from-axioms (all set (lam a (all set (lam b (imp (In a b) (imp (imp (In a b) (In b a)) (In b a))))))) (pf:all-intro (pf:all-intro (pf:imp-intro (pf:imp-intro (pf:imp-elim (pf:hyp 0) (pf:hyp 1)))))))
