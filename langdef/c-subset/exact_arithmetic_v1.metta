(GSLTLanguageDefWireV1
  "ExactArithmetic"
  (LCons (TypeDecl "Integer" CarrierBuiltinInt)
    (LCons (TypeDecl "Operation" CarrierAst)
      (LCons (TypeDecl "Outcome" CarrierAst)
        (LCons (TypeDecl "Config" CarrierAst) LNil))))
  (LCons
    (GrammarRule "arith:add" "Operation" LNil
      (LCons (SyntaxTerminal "ArithAdd") LNil) EvalNone)
    (LCons
      (GrammarRule "arith:sub" "Operation" LNil
        (LCons (SyntaxTerminal "ArithSub") LNil) EvalNone)
      (LCons
        (GrammarRule "arith:mul" "Operation" LNil
          (LCons (SyntaxTerminal "ArithMul") LNil) EvalNone)
        (LCons
          (GrammarRule "arith:tquot" "Operation" LNil
            (LCons (SyntaxTerminal "ArithTQuot") LNil) EvalNone)
          (LCons
            (GrammarRule "arith:fquot" "Operation" LNil
              (LCons (SyntaxTerminal "ArithFQuot") LNil) EvalNone)
            (LCons
              (GrammarRule "arith:trem" "Operation" LNil
                (LCons (SyntaxTerminal "ArithTRem") LNil) EvalNone)
              (LCons
                (GrammarRule "arith:frem" "Operation" LNil
                  (LCons (SyntaxTerminal "ArithFRem") LNil) EvalNone)
                (LCons
                  (GrammarRule "arith:outcome-value" "Outcome"
                    (LCons (TermSimple "value" (TBase "Integer")) LNil)
                    (LCons (SyntaxTerminal "ArithValue") LNil) EvalNone)
                  (LCons
                    (GrammarRule "arith:outcome-declined" "Outcome" LNil
                      (LCons (SyntaxTerminal "ArithDeclined") LNil) EvalNone)
                    (LCons
                      (GrammarRule "arith:eval" "Config"
                        (LCons (TermSimple "operation" (TBase "Operation"))
                          (LCons (TermSimple "first" (TBase "Integer"))
                            (LCons (TermSimple "second" (TBase "Integer")) LNil)))
                        (LCons (SyntaxTerminal "ArithEval") LNil)
                        (EvalSome EvalRewrite))
                      (LCons
                        (GrammarRule "arith:halted" "Config"
                          (LCons (TermSimple "outcome" (TBase "Outcome")) LNil)
                          (LCons (SyntaxTerminal "ArithHalted") LNil) EvalNone)
                        LNil)))))))))))
  LNil
  (LCons
    (RewriteRule "arith:evaluate:arith:add"
      (LCons (TypeBinding "first" (TBase "Integer"))
        (LCons (TypeBinding "second" (TBase "Integer"))
          (LCons (TypeBinding "outcome" (TBase "Outcome")) LNil)))
      (LCons
        (RelationQuery "ExactIntegerAdd"
          (LCons (FVar "first")
            (LCons (FVar "second")
              (LCons (FVar "outcome") LNil))))
        LNil)
      (PApp "arith:eval"
        (LCons (PApp "arith:add" LNil)
          (LCons (FVar "first") (LCons (FVar "second") LNil))))
      (PApp "arith:halted" (LCons (FVar "outcome") LNil)))
    (LCons
      (RewriteRule "arith:evaluate:arith:sub"
        (LCons (TypeBinding "first" (TBase "Integer"))
          (LCons (TypeBinding "second" (TBase "Integer"))
            (LCons (TypeBinding "outcome" (TBase "Outcome")) LNil)))
        (LCons
          (RelationQuery "ExactIntegerSub"
            (LCons (FVar "first")
              (LCons (FVar "second")
                (LCons (FVar "outcome") LNil))))
          LNil)
        (PApp "arith:eval"
          (LCons (PApp "arith:sub" LNil)
            (LCons (FVar "first") (LCons (FVar "second") LNil))))
        (PApp "arith:halted" (LCons (FVar "outcome") LNil)))
      (LCons
        (RewriteRule "arith:evaluate:arith:mul"
          (LCons (TypeBinding "first" (TBase "Integer"))
            (LCons (TypeBinding "second" (TBase "Integer"))
              (LCons (TypeBinding "outcome" (TBase "Outcome")) LNil)))
          (LCons
            (RelationQuery "ExactIntegerMul"
              (LCons (FVar "first")
                (LCons (FVar "second")
                  (LCons (FVar "outcome") LNil))))
            LNil)
          (PApp "arith:eval"
            (LCons (PApp "arith:mul" LNil)
              (LCons (FVar "first") (LCons (FVar "second") LNil))))
          (PApp "arith:halted" (LCons (FVar "outcome") LNil)))
        (LCons
          (RewriteRule "arith:evaluate:arith:tquot"
            (LCons (TypeBinding "first" (TBase "Integer"))
              (LCons (TypeBinding "second" (TBase "Integer"))
                (LCons (TypeBinding "outcome" (TBase "Outcome")) LNil)))
            (LCons
              (RelationQuery "ExactIntegerTQuot"
                (LCons (FVar "first")
                  (LCons (FVar "second")
                    (LCons (FVar "outcome") LNil))))
              LNil)
            (PApp "arith:eval"
              (LCons (PApp "arith:tquot" LNil)
                (LCons (FVar "first") (LCons (FVar "second") LNil))))
            (PApp "arith:halted" (LCons (FVar "outcome") LNil)))
          (LCons
            (RewriteRule "arith:evaluate:arith:fquot"
              (LCons (TypeBinding "first" (TBase "Integer"))
                (LCons (TypeBinding "second" (TBase "Integer"))
                  (LCons (TypeBinding "outcome" (TBase "Outcome")) LNil)))
              (LCons
                (RelationQuery "ExactIntegerFQuot"
                  (LCons (FVar "first")
                    (LCons (FVar "second")
                      (LCons (FVar "outcome") LNil))))
                LNil)
              (PApp "arith:eval"
                (LCons (PApp "arith:fquot" LNil)
                  (LCons (FVar "first") (LCons (FVar "second") LNil))))
              (PApp "arith:halted" (LCons (FVar "outcome") LNil)))
            (LCons
              (RewriteRule "arith:evaluate:arith:trem"
                (LCons (TypeBinding "first" (TBase "Integer"))
                  (LCons (TypeBinding "second" (TBase "Integer"))
                    (LCons (TypeBinding "outcome" (TBase "Outcome")) LNil)))
                (LCons
                  (RelationQuery "ExactIntegerTRem"
                    (LCons (FVar "first")
                      (LCons (FVar "second")
                        (LCons (FVar "outcome") LNil))))
                  LNil)
                (PApp "arith:eval"
                  (LCons (PApp "arith:trem" LNil)
                    (LCons (FVar "first") (LCons (FVar "second") LNil))))
                (PApp "arith:halted" (LCons (FVar "outcome") LNil)))
              (LCons
                (RewriteRule "arith:evaluate:arith:frem"
                  (LCons (TypeBinding "first" (TBase "Integer"))
                    (LCons (TypeBinding "second" (TBase "Integer"))
                      (LCons (TypeBinding "outcome" (TBase "Outcome")) LNil)))
                  (LCons
                    (RelationQuery "ExactIntegerFRem"
                      (LCons (FVar "first")
                        (LCons (FVar "second")
                          (LCons (FVar "outcome") LNil))))
                    LNil)
                  (PApp "arith:eval"
                    (LCons (PApp "arith:frem" LNil)
                      (LCons (FVar "first") (LCons (FVar "second") LNil))))
                  (PApp "arith:halted" (LCons (FVar "outcome") LNil)))
                LNil))))))))
