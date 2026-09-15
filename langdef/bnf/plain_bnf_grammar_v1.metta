(GSLTLanguageDefWireV1 "PlainBnfGrammarV1"
  (LCons (TypeDecl "Integer" CarrierBuiltinInt)
  (LCons (TypeDecl "String" CarrierBuiltinString)
  (LCons (TypeDecl "BnfText" CarrierAst)
  (LCons (TypeDecl "BnfSourceSpan" CarrierAst)
  (LCons (TypeDecl "BnfGrammarDocument" CarrierAst)
  (LCons (TypeDecl "BnfGrammarEntries" CarrierAst)
  (LCons (TypeDecl "BnfGrammarEntry" CarrierAst)
  (LCons (TypeDecl "BnfGrammarExpression" CarrierAst)
  (LCons (TypeDecl "BnfGrammarAlternatives" CarrierAst)
  (LCons (TypeDecl "BnfGrammarAlternative" CarrierAst)
  (LCons (TypeDecl "BnfGrammarElements" CarrierAst)
  (LCons (TypeDecl "BnfGrammarElement" CarrierAst)
  (LCons (TypeDecl "BnfGrammarInput" CarrierAst)
  (LCons (TypeDecl "BnfGrammarAuthority" CarrierAst)
  (LCons (TypeDecl "BnfStartSelection" CarrierAst)
  (LCons (TypeDecl "BnfLexicalEnvironment" CarrierAst)
  (LCons (TypeDecl "BnfLexicalDeclarations" CarrierAst)
  (LCons (TypeDecl "BnfLexicalDeclaration" CarrierAst)
  (LCons (TypeDecl "BnfLexicalMatcher" CarrierAst)
  (LCons (TypeDecl "BnfScalarList" CarrierAst)
  (LCons (TypeDecl "BnfLexicalOrigin" CarrierAst)
  LNil)))))))))))))))))))))

  (LCons (GrammarRule "bnf-v1:text-nil" "BnfText"
    LNil LNil EvalNone)
  (LCons (GrammarRule "bnf-v1:text-cons" "BnfText"
    (LCons (TermSimple "scalar" (TBase "Integer"))
    (LCons (TermSimple "tail" (TBase "BnfText")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "bnf-v1:source-span" "BnfSourceSpan"
    (LCons (TermSimple "start" (TBase "Integer"))
    (LCons (TermSimple "stop" (TBase "Integer")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "bnf-v1:document" "BnfGrammarDocument"
    (LCons (TermSimple "entries" (TBase "BnfGrammarEntries"))
    (LCons (TermSimple "span" (TBase "BnfSourceSpan")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "bnf-v1:entries-nil" "BnfGrammarEntries"
    LNil LNil EvalNone)
  (LCons (GrammarRule "bnf-v1:entries-cons" "BnfGrammarEntries"
    (LCons (TermSimple "head" (TBase "BnfGrammarEntry"))
    (LCons (TermSimple "tail" (TBase "BnfGrammarEntries")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "bnf-v1:rule" "BnfGrammarEntry"
    (LCons (TermSimple "name" (TBase "BnfText"))
    (LCons (TermSimple "expression" (TBase "BnfGrammarExpression"))
    (LCons (TermSimple "span" (TBase "BnfSourceSpan")) LNil)))
    LNil EvalNone)
  (LCons (GrammarRule "bnf-v1:comment" "BnfGrammarEntry"
    (LCons (TermSimple "text" (TBase "BnfText"))
    (LCons (TermSimple "span" (TBase "BnfSourceSpan")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "bnf-v1:blank" "BnfGrammarEntry"
    (LCons (TermSimple "span" (TBase "BnfSourceSpan")) LNil)
    LNil EvalNone)
  (LCons (GrammarRule "bnf-v1:expression" "BnfGrammarExpression"
    (LCons (TermSimple "alternatives" (TBase "BnfGrammarAlternatives"))
    (LCons (TermSimple "span" (TBase "BnfSourceSpan")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "bnf-v1:alternatives-nil" "BnfGrammarAlternatives"
    LNil LNil EvalNone)
  (LCons (GrammarRule "bnf-v1:alternatives-cons" "BnfGrammarAlternatives"
    (LCons (TermSimple "head" (TBase "BnfGrammarAlternative"))
    (LCons (TermSimple "tail" (TBase "BnfGrammarAlternatives")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "bnf-v1:alternative" "BnfGrammarAlternative"
    (LCons (TermSimple "elements" (TBase "BnfGrammarElements"))
    (LCons (TermSimple "span" (TBase "BnfSourceSpan")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "bnf-v1:elements-nil" "BnfGrammarElements"
    LNil LNil EvalNone)
  (LCons (GrammarRule "bnf-v1:elements-cons" "BnfGrammarElements"
    (LCons (TermSimple "head" (TBase "BnfGrammarElement"))
    (LCons (TermSimple "tail" (TBase "BnfGrammarElements")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "bnf-v1:reference" "BnfGrammarElement"
    (LCons (TermSimple "name" (TBase "BnfText"))
    (LCons (TermSimple "span" (TBase "BnfSourceSpan")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "bnf-v1:literal" "BnfGrammarElement"
    (LCons (TermSimple "text" (TBase "BnfText"))
    (LCons (TermSimple "span" (TBase "BnfSourceSpan")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "bnf-v1:grammar-input" "BnfGrammarInput"
    (LCons (TermSimple "document" (TBase "BnfGrammarDocument"))
    (LCons (TermSimple "authority" (TBase "BnfGrammarAuthority")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "bnf-v1:grammar-authority" "BnfGrammarAuthority"
    (LCons (TermSimple "start" (TBase "BnfStartSelection"))
    (LCons (TermSimple "lexical" (TBase "BnfLexicalEnvironment")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "bnf-v1:start" "BnfStartSelection"
    (LCons (TermSimple "name" (TBase "BnfText")) LNil)
    LNil EvalNone)
  (LCons (GrammarRule "bnf-v1:lexical-environment" "BnfLexicalEnvironment"
    (LCons (TermSimple "declarations" (TBase "BnfLexicalDeclarations")) LNil)
    LNil EvalNone)
  (LCons (GrammarRule "bnf-v1:lexical-declarations-nil" "BnfLexicalDeclarations"
    LNil LNil EvalNone)
  (LCons (GrammarRule "bnf-v1:lexical-declarations-cons" "BnfLexicalDeclarations"
    (LCons (TermSimple "head" (TBase "BnfLexicalDeclaration"))
    (LCons (TermSimple "tail" (TBase "BnfLexicalDeclarations")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "bnf-v1:lexical-declaration" "BnfLexicalDeclaration"
    (LCons (TermSimple "reference-name" (TBase "BnfText"))
    (LCons (TermSimple "class-name" (TBase "String"))
    (LCons (TermSimple "matcher" (TBase "BnfLexicalMatcher"))
    (LCons (TermSimple "label" (TBase "String"))
    (LCons (TermSimple "origin" (TBase "BnfLexicalOrigin")) LNil)))))
    LNil EvalNone)
  (LCons (GrammarRule "bnf-v1:lexical-points" "BnfLexicalMatcher"
    (LCons (TermSimple "scalars" (TBase "BnfScalarList")) LNil)
    LNil EvalNone)
  (LCons (GrammarRule "bnf-v1:lexical-except" "BnfLexicalMatcher"
    (LCons (TermSimple "scalars" (TBase "BnfScalarList")) LNil)
    LNil EvalNone)
  (LCons (GrammarRule "bnf-v1:scalars-nil" "BnfScalarList"
    LNil LNil EvalNone)
  (LCons (GrammarRule "bnf-v1:scalars-cons" "BnfScalarList"
    (LCons (TermSimple "head" (TBase "Integer"))
    (LCons (TermSimple "tail" (TBase "BnfScalarList")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "bnf-v1:lexical-origin" "BnfLexicalOrigin"
    (LCons (TermSimple "authority" (TBase "String"))
    (LCons (TermSimple "occurrence" (TBase "Integer")) LNil))
    LNil EvalNone)
  LNil)))))))))))))))))))))))))))))
  LNil
  LNil)
