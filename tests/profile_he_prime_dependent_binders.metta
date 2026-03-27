; he_prime binder-aware telescope conformance.
(: Fact Type)
(: fact Fact)
(: other Fact)
(: WitnessOf (-> $p Type))
(: remember (-> (: $p Fact) (WitnessOf $p)))

!(assertEqual
  (get-type (remember fact))
  (WitnessOf fact))
!(assertEqualToResult
  (remember fact)
  ((remember fact)))

(: goodWitness (WitnessOf fact))
(: otherWitness (WitnessOf other))
(: Checked (-> $p $w Type))
(: useWitness (-> (: $p Fact) (: $w (WitnessOf $p)) (Checked $p $w)))

!(assertEqual
  (get-type (useWitness fact goodWitness))
  (Checked fact goodWitness))
!(assertEqualToResult
  (useWitness fact goodWitness)
  ((useWitness fact goodWitness)))
!(assertEqualToResult
  (get-type (useWitness fact otherWitness))
  ())

; Mini bio-style proof-rule shape: unary and binary dependent binders.
(: Snp Type)
(: Gene Type)
(: rs1 Snp)
(: rs2 Snp)
(: gene1 Gene)
(: eqtl (-> Snp Gene Type))
(: target (-> Snp Gene Type))
(: coreg (-> Snp Snp Type))
(: eqtl_rs1_gene1 (eqtl rs1 gene1))
(: target_rs2_gene1 (target rs2 gene1))
(: wrong_target_rs1_gene1 (target rs1 gene1))
(: coreg_rs1_rs2 (coreg rs1 rs2))
(: dt (-> (: $e (eqtl $snp $gene)) (target $snp $gene)))
(: ct (-> (: $e1 (coreg $snp1 $snp2))
          (: $e2 (target $snp2 $gene))
          (target $snp1 $gene)))

!(assertEqual
  (get-type (dt eqtl_rs1_gene1))
  (target rs1 gene1))
!(assertEqual
  (get-type (ct coreg_rs1_rs2 target_rs2_gene1))
  (target rs1 gene1))
!(assertEqualToResult
  (ct coreg_rs1_rs2 target_rs2_gene1)
  ((ct coreg_rs1_rs2 target_rs2_gene1)))
!(assertEqualToResult
  (get-type (ct coreg_rs1_rs2 wrong_target_rs1_gene1))
  ())
