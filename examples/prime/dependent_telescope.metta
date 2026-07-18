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

(= (show-status $label
      (PrimeVerdict $status $judgment $evidence))
   ($label $status))

!(show-status DependentCodomain
   (prime-judge &self (Check (remember fact) (WitnessOf fact))))
!(show-status MatchingWitness
   (prime-judge &self (Check (use-witness fact fact-witness) (Checked fact))))
!(show-status MismatchedWitness
   (prime-judge &self (Check (use-witness fact other-witness) (Checked fact))))
