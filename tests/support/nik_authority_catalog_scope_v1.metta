; The gate fills DIGEST from the exact presentation line.
(nik-authority-catalog-v1
  (authority TEST "test.λ\\\"\t\x0d" "1" "DIGEST"
    (GInferenceLanguageV1 1 LNil (LCons (JDecl "J" 1) LNil) (LCons (GRuleV1 "ax" LNil LNil (PApp "J" (LCons (PLam BNone (Var 0)) LNil)) LNil) LNil) GNoConversion)
    (positive (PApp "J" (LCons (PLam BNone (Var 0)) LNil)) (GProof (GRuleInst "ax" LNil) PrNil))))
