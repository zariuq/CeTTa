; FORK-HO: which face produced a fact?  A native proof, a native decision,
; and a replayed external certificate answer differently and must stay
; distinguishable.

; Native proof service: a checked derivation, retained on publication.
!(set:theorem native-fact (In Empty (Power Empty)) (pf:all-elim (pf:known PowerI) Empty))
!(match &self (set:known native-fact $p $origin $proof $d $r $sig) $origin)

; Native decision service: an operational observation of a producer.
(: Thing (u 0))
(: it Thing)
(= (produce) it)
!(lang:native-type prime (produce) Thing)

; Certificate boundary: an external authority's proof object replayed.
(= (dtt-goal)
   (PApp "T" (LCons (PApp "C" LNil) (LCons (PApp "Z" LNil) (LCons (PApp "N" LNil) LNil)))))
!(let $goal (dtt-goal) (nik:check DTT $goal (GProof (GRuleInst "z" LNil) PrNil)))
!(let $goal (dtt-goal) (nik:check DTT $goal (GProof (GRuleInst "z" LNil) BrokenChildren)))
!(let $goal (dtt-goal) (nik:check UnknownAuthority $goal (GProof (GRuleInst "z" LNil) PrNil)))
