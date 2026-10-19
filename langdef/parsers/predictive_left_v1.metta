(SyntaxGSLTV1
  (GSLTLanguageDefWireV1 "PredictiveLeft"
    (LCons (TypeDecl "E" CarrierAst) LNil)
    (LCons (GrammarRule "Left" "E" LNil (LCons (SyntaxTerminal "a") LNil) EvalNone) LNil)
    LNil LNil)
  (GSLTParserProfileLayerV1 "PredictiveLeft" "E" LNil LNil)
  (SyntaxLexicalPolicyV1 ScalarScannerlessAllV1)
  (SyntaxOriginsV1
    (LCons (SyntaxOriginV1 "shared-predictive-category" 0) LNil)
    (LCons (SyntaxOriginV1 "predictive-left" 0) LNil)
    LNil LNil))
