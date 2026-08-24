; A dependent result type remembers the exact input witness.

(: Fact Type)
(: fact Fact)
(: other Fact)
(: WitnessOf (-> Fact Type))
(: remember (-> (: $p Fact) (WitnessOf $p)))

(: fact-witness (WitnessOf fact))
(: other-witness (WitnessOf other))
(: Checked (-> Fact Type))
(: use-witness
   (-> (: $p Fact) (: $w (WitnessOf $p)) (Checked $p)))

!(type:check (remember fact) (WitnessOf fact))
!(type:check (use-witness fact fact-witness) (Checked fact))
!(type:check (use-witness fact other-witness) (Checked fact))
