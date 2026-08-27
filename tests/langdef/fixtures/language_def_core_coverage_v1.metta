(GSLTLanguageDefWireV1
  "CoreCoverage"
  (LCons (TypeDecl "Ast" CarrierAst)
    (LCons (TypeDecl "Label" CarrierTokenLabel)
      (LCons (TypeDecl "Raw" CarrierTokenRaw)
        (LCons (TypeDecl "Proof" CarrierTokenProof)
          (LCons (TypeDecl "Path" CarrierTokenPath)
            (LCons (TypeDecl "Int" CarrierBuiltinInt)
              (LCons (TypeDecl "String" CarrierBuiltinString)
                (LCons (TypeDecl "Bool" CarrierBuiltinBool) LNil))))))))
  (LCons
    (GrammarRule "rich" "Ast"
      (LCons (TermSimple "plain" (TBase "Ast"))
        (LCons
          (TermAbstractionNamed
            (BSome "binder")
            "body"
            (TArrow (TBase "Ast") (TBase "Ast")))
          (LCons
            (TermMultiAbstractionNamed
              (LCons "x" (LCons "y" LNil))
              "many"
              (TCollection
                "Mettapedia.OSLF.MeTTaIL.Syntax.CollType.hashBag"
                (TMultiBinder (TBase "Ast"))))
            LNil)))
      (LCons (SyntaxTerminal "kw")
        (LCons (SyntaxNonTerminal "plain")
          (LCons (SyntaxSeparator ",")
            (LCons (SyntaxDelimiter "(" ")")
              (LCons (SyntaxOp (SyntaxVar "plain"))
                (LCons (SyntaxOp (SyntaxSep "many" "," OpNone))
                  (LCons
                    (SyntaxOp
                      (SyntaxSep "many" ";"
                        (OpSome (SyntaxVar "plain"))))
                    (LCons (SyntaxOp (SyntaxZip "plain" "many"))
                      (LCons
                        (SyntaxOp
                          (SyntaxMap
                            (SyntaxVar "many")
                            (LCons "element" LNil)
                            (LCons (SyntaxNonTerminal "element") LNil)))
                        (LCons
                          (SyntaxOp
                            (SyntaxOpt
                              (LCons (SyntaxTerminal "optional") LNil)))
                          LNil))))))))))
      EvalNone)
    (LCons
      (GrammarRule "rewrite-eval" "Ast" LNil LNil
        (EvalSome EvalRewrite))
      (LCons
        (GrammarRule "fold-eval" "Ast" LNil LNil
          (EvalSome EvalFold))
        (LCons
          (GrammarRule "oracle-eval" "Ast" LNil LNil
            (EvalSome EvalOracle))
          LNil))))
  (LCons
    (Equation "all-patterns"
      (LCons (TypeBinding "meta" (TBase "Ast")) LNil)
      (LCons (Freshness "fresh" (FVar "free"))
        (LCons
          (Congruence
            (Var 0)
            (PLam BNone (FVar "lambda-body")))
          (LCons
            (RelationQuery "R"
              (LCons
                (PApp "Ctor" (LCons (FVar "argument") LNil))
                (LCons
                  (PMultiLam 2 (LCons "x" (LCons "y" LNil))
                    (PSubst (Var 1) (FVar "replacement")))
                  (LCons
                    (PCollection
                      "Mettapedia.OSLF.MeTTaIL.Syntax.CollType.vec"
                      (LCons (FVar "element") LNil)
                      (RSome "tail"))
                    LNil))))
            (LCons
              (ForAll "many" "element"
                (Freshness "nested"
                  (PCollection
                    "Mettapedia.OSLF.MeTTaIL.Syntax.CollType.hashSet"
                    LNil RNone)))
              LNil))))
      (PApp "Left" LNil)
      (PApp "Right" LNil))
    LNil)
  (LCons
    (RewriteRule "directional"
      LNil LNil
      (PCollection
        "Mettapedia.OSLF.MeTTaIL.Syntax.CollType.hashBag"
        LNil RNone)
      (FVar "done"))
    LNil))
