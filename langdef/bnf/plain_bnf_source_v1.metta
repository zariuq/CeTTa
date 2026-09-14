(GSLTLanguageDefWireV1 "PlainBnfSourceV1"
  (LCons (TypeDecl "BnfDocument" CarrierAst)
  (LCons (TypeDecl "BnfLines" CarrierAst)
  (LCons (TypeDecl "BnfLine" CarrierAst)
  (LCons (TypeDecl "BnfRuleLine" CarrierAst)
  (LCons (TypeDecl "BnfCommentLine" CarrierAst)
  (LCons (TypeDecl "BnfBlankLine" CarrierAst)
  (LCons (TypeDecl "BnfRuleName" CarrierAst)
  (LCons (TypeDecl "BnfRuleNameTail" CarrierAst)
  (LCons (TypeDecl "BnfNameChar" CarrierAst)
  (LCons (TypeDecl "BnfExpression" CarrierAst)
  (LCons (TypeDecl "BnfAlternativeTail" CarrierAst)
  (LCons (TypeDecl "BnfSequence" CarrierAst)
  (LCons (TypeDecl "BnfSequenceTail" CarrierAst)
  (LCons (TypeDecl "BnfElement" CarrierAst)
  (LCons (TypeDecl "BnfReference" CarrierAst)
  (LCons (TypeDecl "BnfLiteral" CarrierAst)
  (LCons (TypeDecl "BnfLiteralChars" CarrierAst)
  (LCons (TypeDecl "BnfLiteralChar" CarrierAst)
  (LCons (TypeDecl "BnfLiteralScalar" CarrierAst)
  (LCons (TypeDecl "BnfEscape" CarrierAst)
  (LCons (TypeDecl "BnfHorizontalSpaces" CarrierAst)
  (LCons (TypeDecl "BnfHorizontalSpacesOne" CarrierAst)
  (LCons (TypeDecl "BnfHorizontalSpace" CarrierAst)
  (LCons (TypeDecl "BnfCommentChars" CarrierAst)
  (LCons (TypeDecl "BnfLineScalar" CarrierAst)
  (LCons (TypeDecl "BnfLineEnd" CarrierAst)
  LNil))))))))))))))))))))))))))

  (LCons (GrammarRule "bnf-source-v1:document" "BnfDocument"
    (LCons (TermSimple "lines" (TBase "BnfLines")) LNil)
    (LCons (SyntaxNonTerminal "lines") LNil) EvalNone)
  (LCons (GrammarRule "bnf-source-v1:lines-empty" "BnfLines"
    LNil LNil EvalNone)
  (LCons (GrammarRule "bnf-source-v1:lines-cons" "BnfLines"
    (LCons (TermSimple "head" (TBase "BnfLine"))
    (LCons (TermSimple "tail" (TBase "BnfLines")) LNil))
    (LCons (SyntaxNonTerminal "head")
    (LCons (SyntaxNonTerminal "tail") LNil)) EvalNone)
  (LCons (GrammarRule "bnf-source-v1:line-rule" "BnfLine"
    (LCons (TermSimple "rule" (TBase "BnfRuleLine")) LNil)
    (LCons (SyntaxNonTerminal "rule") LNil) EvalNone)
  (LCons (GrammarRule "bnf-source-v1:line-comment" "BnfLine"
    (LCons (TermSimple "comment" (TBase "BnfCommentLine")) LNil)
    (LCons (SyntaxNonTerminal "comment") LNil) EvalNone)
  (LCons (GrammarRule "bnf-source-v1:line-blank" "BnfLine"
    (LCons (TermSimple "blank" (TBase "BnfBlankLine")) LNil)
    (LCons (SyntaxNonTerminal "blank") LNil) EvalNone)

  (LCons (GrammarRule "bnf-source-v1:rule" "BnfRuleLine"
    (LCons (TermSimple "name" (TBase "BnfRuleName"))
    (LCons (TermSimple "before-assign" (TBase "BnfHorizontalSpaces"))
    (LCons (TermSimple "after-assign" (TBase "BnfHorizontalSpaces"))
    (LCons (TermSimple "expression" (TBase "BnfExpression"))
    (LCons (TermSimple "end" (TBase "BnfLineEnd")) LNil)))))
    (LCons (SyntaxTerminal "<")
    (LCons (SyntaxNonTerminal "name")
    (LCons (SyntaxTerminal ">")
    (LCons (SyntaxNonTerminal "before-assign")
    (LCons (SyntaxTerminal "::=")
    (LCons (SyntaxNonTerminal "after-assign")
    (LCons (SyntaxNonTerminal "expression")
    (LCons (SyntaxNonTerminal "end") LNil)))))))) EvalNone)
  (LCons (GrammarRule "bnf-source-v1:comment" "BnfCommentLine"
    (LCons (TermSimple "body" (TBase "BnfCommentChars"))
    (LCons (TermSimple "end" (TBase "BnfLineEnd")) LNil))
    (LCons (SyntaxTerminal "%")
    (LCons (SyntaxNonTerminal "body")
    (LCons (SyntaxNonTerminal "end") LNil))) EvalNone)
  (LCons (GrammarRule "bnf-source-v1:blank" "BnfBlankLine"
    (LCons (TermSimple "spaces" (TBase "BnfHorizontalSpaces"))
    (LCons (TermSimple "end" (TBase "BnfLineEnd")) LNil))
    (LCons (SyntaxNonTerminal "spaces")
    (LCons (SyntaxNonTerminal "end") LNil)) EvalNone)

  (LCons (GrammarRule "bnf-source-v1:name" "BnfRuleName"
    (LCons (TermSimple "head" (TBase "BnfNameChar"))
    (LCons (TermSimple "tail" (TBase "BnfRuleNameTail")) LNil))
    (LCons (SyntaxNonTerminal "head")
    (LCons (SyntaxNonTerminal "tail") LNil)) EvalNone)
  (LCons (GrammarRule "bnf-source-v1:name-tail-empty" "BnfRuleNameTail"
    LNil LNil EvalNone)
  (LCons (GrammarRule "bnf-source-v1:name-tail-cons" "BnfRuleNameTail"
    (LCons (TermSimple "head" (TBase "BnfNameChar"))
    (LCons (TermSimple "tail" (TBase "BnfRuleNameTail")) LNil))
    (LCons (SyntaxNonTerminal "head")
    (LCons (SyntaxNonTerminal "tail") LNil)) EvalNone)

  (LCons (GrammarRule "bnf-source-v1:expression" "BnfExpression"
    (LCons (TermSimple "first" (TBase "BnfSequence"))
    (LCons (TermSimple "tail" (TBase "BnfAlternativeTail")) LNil))
    (LCons (SyntaxNonTerminal "first")
    (LCons (SyntaxNonTerminal "tail") LNil)) EvalNone)
  (LCons (GrammarRule "bnf-source-v1:alternatives-empty" "BnfAlternativeTail"
    LNil LNil EvalNone)
  (LCons (GrammarRule "bnf-source-v1:alternatives-cons" "BnfAlternativeTail"
    (LCons (TermSimple "after-bar" (TBase "BnfHorizontalSpaces"))
    (LCons (TermSimple "next" (TBase "BnfSequence"))
    (LCons (TermSimple "tail" (TBase "BnfAlternativeTail")) LNil)))
    (LCons (SyntaxTerminal " |")
    (LCons (SyntaxNonTerminal "after-bar")
    (LCons (SyntaxNonTerminal "next")
    (LCons (SyntaxNonTerminal "tail") LNil)))) EvalNone)
  (LCons (GrammarRule "bnf-source-v1:sequence-empty" "BnfSequence"
    LNil LNil EvalNone)
  (LCons (GrammarRule "bnf-source-v1:sequence-cons" "BnfSequence"
    (LCons (TermSimple "head" (TBase "BnfElement"))
    (LCons (TermSimple "tail" (TBase "BnfSequenceTail")) LNil))
    (LCons (SyntaxNonTerminal "head")
    (LCons (SyntaxNonTerminal "tail") LNil)) EvalNone)
  (LCons (GrammarRule "bnf-source-v1:sequence-tail-empty" "BnfSequenceTail"
    LNil LNil EvalNone)
  (LCons (GrammarRule "bnf-source-v1:sequence-tail-cons" "BnfSequenceTail"
    (LCons (TermSimple "separator" (TBase "BnfHorizontalSpacesOne"))
    (LCons (TermSimple "head" (TBase "BnfElement"))
    (LCons (TermSimple "tail" (TBase "BnfSequenceTail")) LNil)))
    (LCons (SyntaxNonTerminal "separator")
    (LCons (SyntaxNonTerminal "head")
    (LCons (SyntaxNonTerminal "tail") LNil))) EvalNone)
  (LCons (GrammarRule "bnf-source-v1:element-reference" "BnfElement"
    (LCons (TermSimple "reference" (TBase "BnfReference")) LNil)
    (LCons (SyntaxNonTerminal "reference") LNil) EvalNone)
  (LCons (GrammarRule "bnf-source-v1:element-literal" "BnfElement"
    (LCons (TermSimple "literal" (TBase "BnfLiteral")) LNil)
    (LCons (SyntaxNonTerminal "literal") LNil) EvalNone)
  (LCons (GrammarRule "bnf-source-v1:reference" "BnfReference"
    (LCons (TermSimple "name" (TBase "BnfRuleName")) LNil)
    (LCons (SyntaxTerminal "<")
    (LCons (SyntaxNonTerminal "name")
    (LCons (SyntaxTerminal ">") LNil))) EvalNone)
  (LCons (GrammarRule "bnf-source-v1:literal" "BnfLiteral"
    (LCons (TermSimple "body" (TBase "BnfLiteralChars")) LNil)
    (LCons (SyntaxTerminal "\"")
    (LCons (SyntaxNonTerminal "body")
    (LCons (SyntaxTerminal "\"") LNil))) EvalNone)
  (LCons (GrammarRule "bnf-source-v1:literal-chars-empty" "BnfLiteralChars"
    LNil LNil EvalNone)
  (LCons (GrammarRule "bnf-source-v1:literal-chars-cons" "BnfLiteralChars"
    (LCons (TermSimple "head" (TBase "BnfLiteralChar"))
    (LCons (TermSimple "tail" (TBase "BnfLiteralChars")) LNil))
    (LCons (SyntaxNonTerminal "head")
    (LCons (SyntaxNonTerminal "tail") LNil)) EvalNone)
  (LCons (GrammarRule "bnf-source-v1:literal-char-scalar" "BnfLiteralChar"
    (LCons (TermSimple "scalar" (TBase "BnfLiteralScalar")) LNil)
    (LCons (SyntaxNonTerminal "scalar") LNil) EvalNone)
  (LCons (GrammarRule "bnf-source-v1:literal-char-escape" "BnfLiteralChar"
    (LCons (TermSimple "escape" (TBase "BnfEscape")) LNil)
    (LCons (SyntaxNonTerminal "escape") LNil) EvalNone)
  (LCons (GrammarRule "bnf-source-v1:escape-quote" "BnfEscape"
    LNil (LCons (SyntaxTerminal "\\\"") LNil) EvalNone)
  (LCons (GrammarRule "bnf-source-v1:escape-backslash" "BnfEscape"
    LNil (LCons (SyntaxTerminal "\\\\") LNil) EvalNone)
  (LCons (GrammarRule "bnf-source-v1:escape-newline" "BnfEscape"
    LNil (LCons (SyntaxTerminal "\\n") LNil) EvalNone)
  (LCons (GrammarRule "bnf-source-v1:escape-return" "BnfEscape"
    LNil (LCons (SyntaxTerminal "\\r") LNil) EvalNone)
  (LCons (GrammarRule "bnf-source-v1:escape-tab" "BnfEscape"
    LNil (LCons (SyntaxTerminal "\\t") LNil) EvalNone)

  (LCons (GrammarRule "bnf-source-v1:spaces-empty" "BnfHorizontalSpaces"
    LNil LNil EvalNone)
  (LCons (GrammarRule "bnf-source-v1:spaces-cons" "BnfHorizontalSpaces"
    (LCons (TermSimple "head" (TBase "BnfHorizontalSpace"))
    (LCons (TermSimple "tail" (TBase "BnfHorizontalSpaces")) LNil))
    (LCons (SyntaxNonTerminal "head")
    (LCons (SyntaxNonTerminal "tail") LNil)) EvalNone)
  (LCons (GrammarRule "bnf-source-v1:spaces-one" "BnfHorizontalSpacesOne"
    (LCons (TermSimple "head" (TBase "BnfHorizontalSpace"))
    (LCons (TermSimple "tail" (TBase "BnfHorizontalSpaces")) LNil))
    (LCons (SyntaxNonTerminal "head")
    (LCons (SyntaxNonTerminal "tail") LNil)) EvalNone)
  (LCons (GrammarRule "bnf-source-v1:comment-chars-empty" "BnfCommentChars"
    LNil LNil EvalNone)
  (LCons (GrammarRule "bnf-source-v1:comment-chars-cons" "BnfCommentChars"
    (LCons (TermSimple "head" (TBase "BnfLineScalar"))
    (LCons (TermSimple "tail" (TBase "BnfCommentChars")) LNil))
    (LCons (SyntaxNonTerminal "head")
    (LCons (SyntaxNonTerminal "tail") LNil)) EvalNone)
  (LCons (GrammarRule "bnf-source-v1:line-end-lf" "BnfLineEnd"
    LNil (LCons (SyntaxTerminal "\n") LNil) EvalNone)
  (LCons (GrammarRule "bnf-source-v1:line-end-crlf" "BnfLineEnd"
    LNil (LCons (SyntaxTerminal "\x0d\n") LNil) EvalNone)
  LNil)))))))))))))))))))))))))))))))))))))))
  LNil
  LNil)
