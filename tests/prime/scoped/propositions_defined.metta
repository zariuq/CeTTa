; Propositions as definitions: falsum and negation are defined, and the
; introduction of double negation is a two-line proof, as in the Coq ladder.
!(import! &self pf)
!(bind! &thy (new-space))
!(set:define &thy falsum prop (= falsum (all prop (lam p p))))
!(set:define &thy neg (-> prop prop) (= (neg $p) (imp $p falsum)))
!(set:known-proposition &thy neg)
; p -> neg (neg p)
!(pf:theorem &thy double-negation-intro (all prop (lam p (imp p (neg (neg p)))))
   (pf:fix p (pf:assume h (pf:assume k (pf:by k h)))))
; from falsum anything: ex falso is the definition unfolded and instantiated
!(pf:theorem &thy ex-falso (all prop (lam q (imp falsum q)))
   (pf:fix q (pf:assume bot (pf:by bot))))
; contraposition: (p -> q) -> neg q -> neg p
!(pf:theorem &thy contraposition
   (all prop (lam p (all prop (lam q (imp (imp p q) (imp (neg q) (neg p)))))))
   (pf:fix p (pf:fix q (pf:assume f (pf:assume nq (pf:assume hp (pf:by nq (pf:by f hp))))))))
!(pf:theory &thy ((: A prop)))
!(set:proves &thy (pf:by contraposition) (imp (imp A A) (imp (neg A) (neg A))))
!(set:proves &thy (pf:by double-negation-intro) (imp A (neg A)))
