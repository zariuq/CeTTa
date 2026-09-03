(GSLTLanguageDefWireV1 "TptpOfficialExtendedBnfAstV1"
  (LCons (TypeDecl "Integer" CarrierBuiltinInt)
  (LCons (TypeDecl "Text" CarrierAst)
  (LCons (TypeDecl "SourceSpan" CarrierAst)
  (LCons (TypeDecl "Document" CarrierAst)
  (LCons (TypeDecl "Entries" CarrierAst)
  (LCons (TypeDecl "Entry" CarrierAst)
  (LCons (TypeDecl "GrammarExpression" CarrierAst)
  (LCons (TypeDecl "GrammarSequences" CarrierAst)
  (LCons (TypeDecl "GrammarSequence" CarrierAst)
  (LCons (TypeDecl "GrammarElements" CarrierAst)
  (LCons (TypeDecl "GrammarElement" CarrierAst)
  (LCons (TypeDecl "GrammarRepetition" CarrierAst)
  (LCons (TypeDecl "RegexExpression" CarrierAst)
  (LCons (TypeDecl "RegexSequences" CarrierAst)
  (LCons (TypeDecl "RegexSequence" CarrierAst)
  (LCons (TypeDecl "RegexElements" CarrierAst)
  (LCons (TypeDecl "RegexElement" CarrierAst)
  (LCons (TypeDecl "RegexAtom" CarrierAst)
  (LCons (TypeDecl "RegexRepetition" CarrierAst)
  (LCons (TypeDecl "RegexClassPolarity" CarrierAst)
  (LCons (TypeDecl "RegexClassItems" CarrierAst)
  (LCons (TypeDecl "RegexClassItem" CarrierAst)
  (LCons (TypeDecl "RegexEndpoint" CarrierAst)
  LNil)))))))))))))))))))))))

  (LCons (GrammarRule "tptp-ebnf-v1:text-nil" "Text"
    LNil LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:text-cons" "Text"
    (LCons (TermSimple "scalar" (TBase "Integer"))
    (LCons (TermSimple "tail" (TBase "Text")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:source-span" "SourceSpan"
    (LCons (TermSimple "start" (TBase "Integer"))
    (LCons (TermSimple "stop" (TBase "Integer")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:document" "Document"
    (LCons (TermSimple "entries" (TBase "Entries"))
    (LCons (TermSimple "span" (TBase "SourceSpan")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:entries-nil" "Entries"
    LNil LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:entries-cons" "Entries"
    (LCons (TermSimple "head" (TBase "Entry"))
    (LCons (TermSimple "tail" (TBase "Entries")) LNil))
    LNil EvalNone)

  (LCons (GrammarRule "tptp-ebnf-v1:syntax-row" "Entry"
    (LCons (TermSimple "name" (TBase "Text"))
    (LCons (TermSimple "expression" (TBase "GrammarExpression"))
    (LCons (TermSimple "span" (TBase "SourceSpan")) LNil)))
    LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:semantic-row" "Entry"
    (LCons (TermSimple "name" (TBase "Text"))
    (LCons (TermSimple "constraint" (TBase "GrammarExpression"))
    (LCons (TermSimple "span" (TBase "SourceSpan")) LNil)))
    LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:token-row" "Entry"
    (LCons (TermSimple "name" (TBase "Text"))
    (LCons (TermSimple "expression" (TBase "RegexExpression"))
    (LCons (TermSimple "span" (TBase "SourceSpan")) LNil)))
    LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:character-row" "Entry"
    (LCons (TermSimple "name" (TBase "Text"))
    (LCons (TermSimple "expression" (TBase "RegexExpression"))
    (LCons (TermSimple "span" (TBase "SourceSpan")) LNil)))
    LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:comment" "Entry"
    (LCons (TermSimple "text" (TBase "Text"))
    (LCons (TermSimple "span" (TBase "SourceSpan")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:blank" "Entry"
    (LCons (TermSimple "span" (TBase "SourceSpan")) LNil)
    LNil EvalNone)

  (LCons (GrammarRule "tptp-ebnf-v1:grammar-choice" "GrammarExpression"
    (LCons (TermSimple "alternatives" (TBase "GrammarSequences"))
    (LCons (TermSimple "span" (TBase "SourceSpan")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:grammar-sequences-nil" "GrammarSequences"
    LNil LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:grammar-sequences-cons" "GrammarSequences"
    (LCons (TermSimple "head" (TBase "GrammarSequence"))
    (LCons (TermSimple "tail" (TBase "GrammarSequences")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:grammar-sequence" "GrammarSequence"
    (LCons (TermSimple "elements" (TBase "GrammarElements"))
    (LCons (TermSimple "span" (TBase "SourceSpan")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:grammar-elements-nil" "GrammarElements"
    LNil LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:grammar-elements-cons" "GrammarElements"
    (LCons (TermSimple "head" (TBase "GrammarElement"))
    (LCons (TermSimple "tail" (TBase "GrammarElements")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:grammar-reference" "GrammarElement"
    (LCons (TermSimple "name" (TBase "Text"))
    (LCons (TermSimple "repetition" (TBase "GrammarRepetition"))
    (LCons (TermSimple "span" (TBase "SourceSpan")) LNil)))
    LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:grammar-literal" "GrammarElement"
    (LCons (TermSimple "text" (TBase "Text"))
    (LCons (TermSimple "span" (TBase "SourceSpan")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:grammar-once" "GrammarRepetition"
    LNil LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:grammar-zero-or-more" "GrammarRepetition"
    LNil LNil EvalNone)

  (LCons (GrammarRule "tptp-ebnf-v1:regex-choice" "RegexExpression"
    (LCons (TermSimple "alternatives" (TBase "RegexSequences"))
    (LCons (TermSimple "span" (TBase "SourceSpan")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:regex-sequences-nil" "RegexSequences"
    LNil LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:regex-sequences-cons" "RegexSequences"
    (LCons (TermSimple "head" (TBase "RegexSequence"))
    (LCons (TermSimple "tail" (TBase "RegexSequences")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:regex-sequence" "RegexSequence"
    (LCons (TermSimple "elements" (TBase "RegexElements"))
    (LCons (TermSimple "span" (TBase "SourceSpan")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:regex-elements-nil" "RegexElements"
    LNil LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:regex-elements-cons" "RegexElements"
    (LCons (TermSimple "head" (TBase "RegexElement"))
    (LCons (TermSimple "tail" (TBase "RegexElements")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:regex-piece" "RegexElement"
    (LCons (TermSimple "atom" (TBase "RegexAtom"))
    (LCons (TermSimple "repetition" (TBase "RegexRepetition"))
    (LCons (TermSimple "span" (TBase "SourceSpan")) LNil)))
    LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:regex-angle-reference" "RegexAtom"
    (LCons (TermSimple "name" (TBase "Text"))
    (LCons (TermSimple "span" (TBase "SourceSpan")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:regex-brace-reference" "RegexAtom"
    (LCons (TermSimple "name" (TBase "Text"))
    (LCons (TermSimple "span" (TBase "SourceSpan")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:regex-group" "RegexAtom"
    (LCons (TermSimple "body" (TBase "RegexExpression"))
    (LCons (TermSimple "span" (TBase "SourceSpan")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:regex-class" "RegexAtom"
    (LCons (TermSimple "polarity" (TBase "RegexClassPolarity"))
    (LCons (TermSimple "items" (TBase "RegexClassItems"))
    (LCons (TermSimple "span" (TBase "SourceSpan")) LNil)))
    LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:regex-wildcard" "RegexAtom"
    (LCons (TermSimple "span" (TBase "SourceSpan")) LNil)
    LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:regex-once" "RegexRepetition"
    LNil LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:regex-zero-or-more" "RegexRepetition"
    LNil LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:regex-class-positive" "RegexClassPolarity"
    LNil LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:regex-class-negated" "RegexClassPolarity"
    LNil LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:regex-class-items-nil" "RegexClassItems"
    LNil LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:regex-class-items-cons" "RegexClassItems"
    (LCons (TermSimple "head" (TBase "RegexClassItem"))
    (LCons (TermSimple "tail" (TBase "RegexClassItems")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:regex-class-singleton" "RegexClassItem"
    (LCons (TermSimple "value" (TBase "RegexEndpoint"))
    (LCons (TermSimple "span" (TBase "SourceSpan")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:regex-class-range" "RegexClassItem"
    (LCons (TermSimple "first" (TBase "RegexEndpoint"))
    (LCons (TermSimple "last" (TBase "RegexEndpoint"))
    (LCons (TermSimple "span" (TBase "SourceSpan")) LNil)))
    LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:regex-endpoint-scalar" "RegexEndpoint"
    (LCons (TermSimple "text" (TBase "Text"))
    (LCons (TermSimple "span" (TBase "SourceSpan")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:regex-endpoint-simple-escape" "RegexEndpoint"
    (LCons (TermSimple "text" (TBase "Text"))
    (LCons (TermSimple "span" (TBase "SourceSpan")) LNil))
    LNil EvalNone)
  (LCons (GrammarRule "tptp-ebnf-v1:regex-endpoint-octal-escape" "RegexEndpoint"
    (LCons (TermSimple "digits" (TBase "Text"))
    (LCons (TermSimple "span" (TBase "SourceSpan")) LNil))
    LNil EvalNone)
  LNil)))))))))))))))))))))))))))))))))))))))))))))
  LNil
  LNil)
