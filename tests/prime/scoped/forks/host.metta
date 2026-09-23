; FORK-HOST: one higher-order statement, or its first-order instances?
; A polymorphic universal is declared once and instantiated at sets,
; propositions and functions; its first-order leaves can be checked one by
; one, but only the higher-order form is the single theorem.

!(bind! &s (new-space))
!(add-atom &s (: set (u 0)))
!(add-atom &s (: prop (u 0)))
!(add-atom &s (: In (-> set (-> set prop))))
!(add-atom &s (: Empty set))
!(add-atom &s (: all (-> (A : (u $l)) (-> (-> A prop) prop))))
!(type:of &s all)
!(type:check &s (all set (lam x (In x x))) prop)
!(type:check &s (all prop (lam p p)) prop)
!(type:check &s (all (-> set prop) (lam P (P Empty))) prop)

; First-order chart: the universal checked leaf by leaf at enumerated witnesses.
(= (witnesses) (superpose (Empty (Power Empty) (UnivOf Empty))))
!(let $w (witnesses)
   (set:proves (pf:all-elim (pf:known PowerI) $w) (In $w (Power $w))))
; Higher-order statement: the same claim once, for every witness.
!(set:proves (pf:all-intro (pf:all-elim (pf:known PowerI) (pf:var 0)))
             (all set (lam x (In x (Power x)))))
