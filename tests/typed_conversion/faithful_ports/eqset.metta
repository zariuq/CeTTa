; The source binary-predicate equality, with its opaque carrier generalized.
!(import! &self pf)
!(set:theorem eq-refl-set (all (u 0) (lam A (all A (lam a (all (-> A A prop) (lam Q (imp (Q a a) (Q a a)))))))) (pf:fix A (pf:fix a (pf:fix Q (pf:assume h h)))))
