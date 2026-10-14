; Test template: the gate fills DIGEST from the exact presentation line.
(nik-authority-catalog-v1
  (authority TEST "test.λ\\\"\t\x0d" "1" "DIGEST"
    (GInferenceLanguageV1 1 (LCons (CDecl "Kλ\\\"" 0) LNil) (LCons (JDecl "J" 1) LNil) (LCons (GRuleV1 "ax" LNil LNil (PApp "J" (LCons (PApp "Kλ\\\"" LNil) LNil)) LNil) LNil) GNoConversion)
    (positive (PApp "J" (LCons (PApp "Kλ\\\"" LNil) LNil)) (GProof (GRuleInst "ax" LNil) PrNil))))
