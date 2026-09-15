(GSLTLanguageDefWireV1 "TptpOfficialExtendedBnfSourceV1"
  (LCons (TypeDecl "SyntaxBnfDocument" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfLines" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfLine" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfCommentLine" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfBlankLine" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfRowLine" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfRuleName" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfRuleNameTail" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfHeaderPadding" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfCommentChars" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfLineScalar" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfNameChar" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfHorizontalSpace" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfLineEnd" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfGrammarExpression" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfGrammarAlternativeTail" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfGrammarSequence" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfGrammarElement" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfGrammarReference" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfGrammarRepetition" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfGrammarLiteral" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfGrammarLiteralScalar" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfGrammarLayout" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfGrammarContinuation" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfRegexExpression" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfRegexAlternativeTail" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfRegexSequence" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfRegexElement" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfRegexPiece" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfRegexAtom" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfRegexRepetition" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfRegexLayout" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfRegexClass" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfRegexClassNegation" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfRegexClassBody" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfRegexClassItem" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfRegexClassEndpoint" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfRegexClassEscape" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfRegexClassScalar" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfRegexSimpleEscapeScalar" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfOctalLeadingThree" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfOctalLeadingTwo" CarrierAst)
  (LCons (TypeDecl "SyntaxBnfOctalDigit" CarrierAst)
  LNil)))))))))))))))))))))))))))))))))))))))))))

  (LCons (GrammarRule "syntax-bnf:document" "SyntaxBnfDocument"
    (LCons (TermSimple "lines" (TBase "SyntaxBnfLines")) LNil)
    (LCons (SyntaxNonTerminal "lines") LNil) EvalNone)
  (LCons (GrammarRule "syntax-bnf:lines-empty" "SyntaxBnfLines" LNil LNil EvalNone)
  (LCons (GrammarRule "syntax-bnf:lines-cons" "SyntaxBnfLines"
    (LCons (TermSimple "head" (TBase "SyntaxBnfLine"))
    (LCons (TermSimple "tail" (TBase "SyntaxBnfLines")) LNil))
    (LCons (SyntaxNonTerminal "head")
    (LCons (SyntaxNonTerminal "tail") LNil)) EvalNone)
  (LCons (GrammarRule "syntax-bnf:line-comment" "SyntaxBnfLine"
    (LCons (TermSimple "comment" (TBase "SyntaxBnfCommentLine")) LNil)
    (LCons (SyntaxNonTerminal "comment") LNil) EvalNone)
  (LCons (GrammarRule "syntax-bnf:line-blank" "SyntaxBnfLine"
    (LCons (TermSimple "blank" (TBase "SyntaxBnfBlankLine")) LNil)
    (LCons (SyntaxNonTerminal "blank") LNil) EvalNone)
  (LCons (GrammarRule "syntax-bnf:line-row" "SyntaxBnfLine"
    (LCons (TermSimple "row" (TBase "SyntaxBnfRowLine")) LNil)
    (LCons (SyntaxNonTerminal "row") LNil) EvalNone)
  (LCons (GrammarRule "syntax-bnf:comment" "SyntaxBnfCommentLine"
    (LCons (TermSimple "body" (TBase "SyntaxBnfCommentChars"))
    (LCons (TermSimple "end" (TBase "SyntaxBnfLineEnd")) LNil))
    (LCons (SyntaxTerminal "%")
    (LCons (SyntaxNonTerminal "body")
    (LCons (SyntaxNonTerminal "end") LNil))) EvalNone)
  (LCons (GrammarRule "syntax-bnf:blank" "SyntaxBnfBlankLine"
    (LCons (TermSimple "end" (TBase "SyntaxBnfLineEnd")) LNil)
    (LCons (SyntaxNonTerminal "end") LNil) EvalNone)

  (LCons (GrammarRule "syntax-bnf:row-syntax" "SyntaxBnfRowLine"
    (LCons (TermSimple "name" (TBase "SyntaxBnfRuleName"))
    (LCons (TermSimple "padding" (TBase "SyntaxBnfHeaderPadding"))
    (LCons (TermSimple "rhs" (TBase "SyntaxBnfGrammarExpression"))
    (LCons (TermSimple "end" (TBase "SyntaxBnfLineEnd")) LNil))))
    (LCons (SyntaxTerminal "<")
    (LCons (SyntaxNonTerminal "name")
    (LCons (SyntaxTerminal ">")
    (LCons (SyntaxNonTerminal "padding")
    (LCons (SyntaxTerminal "::=")
    (LCons (SyntaxNonTerminal "rhs")
    (LCons (SyntaxNonTerminal "end") LNil))))))) EvalNone)
  (LCons (GrammarRule "syntax-bnf:row-semantic" "SyntaxBnfRowLine"
    (LCons (TermSimple "name" (TBase "SyntaxBnfRuleName"))
    (LCons (TermSimple "padding" (TBase "SyntaxBnfHeaderPadding"))
    (LCons (TermSimple "rhs" (TBase "SyntaxBnfGrammarExpression"))
    (LCons (TermSimple "end" (TBase "SyntaxBnfLineEnd")) LNil))))
    (LCons (SyntaxTerminal "<")
    (LCons (SyntaxNonTerminal "name")
    (LCons (SyntaxTerminal ">")
    (LCons (SyntaxNonTerminal "padding")
    (LCons (SyntaxTerminal ":==")
    (LCons (SyntaxNonTerminal "rhs")
    (LCons (SyntaxNonTerminal "end") LNil))))))) EvalNone)
  (LCons (GrammarRule "syntax-bnf:row-token" "SyntaxBnfRowLine"
    (LCons (TermSimple "name" (TBase "SyntaxBnfRuleName"))
    (LCons (TermSimple "padding" (TBase "SyntaxBnfHeaderPadding"))
    (LCons (TermSimple "rhs" (TBase "SyntaxBnfRegexExpression"))
    (LCons (TermSimple "end" (TBase "SyntaxBnfLineEnd")) LNil))))
    (LCons (SyntaxTerminal "<")
    (LCons (SyntaxNonTerminal "name")
    (LCons (SyntaxTerminal ">")
    (LCons (SyntaxNonTerminal "padding")
    (LCons (SyntaxTerminal "::-")
    (LCons (SyntaxNonTerminal "rhs")
    (LCons (SyntaxNonTerminal "end") LNil))))))) EvalNone)
  (LCons (GrammarRule "syntax-bnf:row-character" "SyntaxBnfRowLine"
    (LCons (TermSimple "name" (TBase "SyntaxBnfRuleName"))
    (LCons (TermSimple "padding" (TBase "SyntaxBnfHeaderPadding"))
    (LCons (TermSimple "rhs" (TBase "SyntaxBnfRegexExpression"))
    (LCons (TermSimple "end" (TBase "SyntaxBnfLineEnd")) LNil))))
    (LCons (SyntaxTerminal "<")
    (LCons (SyntaxNonTerminal "name")
    (LCons (SyntaxTerminal ">")
    (LCons (SyntaxNonTerminal "padding")
    (LCons (SyntaxTerminal ":::")
    (LCons (SyntaxNonTerminal "rhs")
    (LCons (SyntaxNonTerminal "end") LNil))))))) EvalNone)

  (LCons (GrammarRule "syntax-bnf:name" "SyntaxBnfRuleName"
    (LCons (TermSimple "head" (TBase "SyntaxBnfNameChar"))
    (LCons (TermSimple "tail" (TBase "SyntaxBnfRuleNameTail")) LNil))
    (LCons (SyntaxNonTerminal "head")
    (LCons (SyntaxNonTerminal "tail") LNil)) EvalNone)
  (LCons (GrammarRule "syntax-bnf:name-tail-empty" "SyntaxBnfRuleNameTail" LNil LNil EvalNone)
  (LCons (GrammarRule "syntax-bnf:name-tail-cons" "SyntaxBnfRuleNameTail"
    (LCons (TermSimple "head" (TBase "SyntaxBnfNameChar"))
    (LCons (TermSimple "tail" (TBase "SyntaxBnfRuleNameTail")) LNil))
    (LCons (SyntaxNonTerminal "head")
    (LCons (SyntaxNonTerminal "tail") LNil)) EvalNone)
  (LCons (GrammarRule "syntax-bnf:padding-one" "SyntaxBnfHeaderPadding"
    (LCons (TermSimple "space" (TBase "SyntaxBnfHorizontalSpace")) LNil)
    (LCons (SyntaxNonTerminal "space") LNil) EvalNone)
  (LCons (GrammarRule "syntax-bnf:padding-cons" "SyntaxBnfHeaderPadding"
    (LCons (TermSimple "head" (TBase "SyntaxBnfHorizontalSpace"))
    (LCons (TermSimple "tail" (TBase "SyntaxBnfHeaderPadding")) LNil))
    (LCons (SyntaxNonTerminal "head")
    (LCons (SyntaxNonTerminal "tail") LNil)) EvalNone)
  (LCons (GrammarRule "syntax-bnf:comment-chars-empty" "SyntaxBnfCommentChars" LNil LNil EvalNone)
  (LCons (GrammarRule "syntax-bnf:comment-chars-cons" "SyntaxBnfCommentChars"
    (LCons (TermSimple "head" (TBase "SyntaxBnfLineScalar"))
    (LCons (TermSimple "tail" (TBase "SyntaxBnfCommentChars")) LNil))
    (LCons (SyntaxNonTerminal "head")
    (LCons (SyntaxNonTerminal "tail") LNil)) EvalNone)
  (LCons (GrammarRule "syntax-bnf:line-end-lf" "SyntaxBnfLineEnd"
    LNil (LCons (SyntaxTerminal "\n") LNil) EvalNone)
  (LCons (GrammarRule "syntax-bnf:line-end-crlf" "SyntaxBnfLineEnd"
    LNil (LCons (SyntaxTerminal "\x0d\n") LNil) EvalNone)

  (LCons (GrammarRule "syntax-bnf:grammar-expression" "SyntaxBnfGrammarExpression"
    (LCons (TermSimple "first" (TBase "SyntaxBnfGrammarSequence"))
    (LCons (TermSimple "tail" (TBase "SyntaxBnfGrammarAlternativeTail")) LNil))
    (LCons (SyntaxNonTerminal "first")
    (LCons (SyntaxNonTerminal "tail") LNil)) EvalNone)
  (LCons (GrammarRule "syntax-bnf:grammar-alternatives-empty" "SyntaxBnfGrammarAlternativeTail"
    LNil LNil EvalNone)
  (LCons (GrammarRule "syntax-bnf:grammar-alternatives-cons" "SyntaxBnfGrammarAlternativeTail"
    (LCons (TermSimple "next" (TBase "SyntaxBnfGrammarSequence"))
    (LCons (TermSimple "tail" (TBase "SyntaxBnfGrammarAlternativeTail")) LNil))
    (LCons (SyntaxTerminal "|")
    (LCons (SyntaxNonTerminal "next")
    (LCons (SyntaxNonTerminal "tail") LNil))) EvalNone)
  (LCons (GrammarRule "syntax-bnf:grammar-sequence-empty" "SyntaxBnfGrammarSequence" LNil LNil EvalNone)
  (LCons (GrammarRule "syntax-bnf:grammar-sequence-cons" "SyntaxBnfGrammarSequence"
    (LCons (TermSimple "head" (TBase "SyntaxBnfGrammarElement"))
    (LCons (TermSimple "tail" (TBase "SyntaxBnfGrammarSequence")) LNil))
    (LCons (SyntaxNonTerminal "head")
    (LCons (SyntaxNonTerminal "tail") LNil)) EvalNone)
  (LCons (GrammarRule "syntax-bnf:grammar-element-reference" "SyntaxBnfGrammarElement"
    (LCons (TermSimple "reference" (TBase "SyntaxBnfGrammarReference")) LNil)
    (LCons (SyntaxNonTerminal "reference") LNil) EvalNone)
  (LCons (GrammarRule "syntax-bnf:grammar-element-literal" "SyntaxBnfGrammarElement"
    (LCons (TermSimple "literal" (TBase "SyntaxBnfGrammarLiteral")) LNil)
    (LCons (SyntaxNonTerminal "literal") LNil) EvalNone)
  (LCons (GrammarRule "syntax-bnf:grammar-element-layout" "SyntaxBnfGrammarElement"
    (LCons (TermSimple "layout" (TBase "SyntaxBnfGrammarLayout")) LNil)
    (LCons (SyntaxNonTerminal "layout") LNil) EvalNone)
  (LCons (GrammarRule "syntax-bnf:grammar-reference" "SyntaxBnfGrammarReference"
    (LCons (TermSimple "name" (TBase "SyntaxBnfRuleName"))
    (LCons (TermSimple "repetition" (TBase "SyntaxBnfGrammarRepetition")) LNil))
    (LCons (SyntaxTerminal "<")
    (LCons (SyntaxNonTerminal "name")
    (LCons (SyntaxTerminal ">")
    (LCons (SyntaxNonTerminal "repetition") LNil)))) EvalNone)
  (LCons (GrammarRule "syntax-bnf:grammar-repetition-one" "SyntaxBnfGrammarRepetition"
    LNil LNil EvalNone)
  (LCons (GrammarRule "syntax-bnf:grammar-repetition-zero-or-more" "SyntaxBnfGrammarRepetition"
    LNil (LCons (SyntaxTerminal "*") LNil) EvalNone)
  (LCons (GrammarRule "syntax-bnf:grammar-literal-scalar" "SyntaxBnfGrammarLiteral"
    (LCons (TermSimple "scalar" (TBase "SyntaxBnfGrammarLiteralScalar")) LNil)
    (LCons (SyntaxNonTerminal "scalar") LNil) EvalNone)
  (LCons (GrammarRule "syntax-bnf:grammar-literal-equiv" "SyntaxBnfGrammarLiteral"
    LNil (LCons (SyntaxTerminal "<=>") LNil) EvalNone)
  (LCons (GrammarRule "syntax-bnf:grammar-literal-reverse-imp" "SyntaxBnfGrammarLiteral"
    LNil (LCons (SyntaxTerminal "<=") LNil) EvalNone)
  (LCons (GrammarRule "syntax-bnf:grammar-literal-xor" "SyntaxBnfGrammarLiteral"
    LNil (LCons (SyntaxTerminal "<~>") LNil) EvalNone)
  (LCons (GrammarRule "syntax-bnf:grammar-literal-subtype" "SyntaxBnfGrammarLiteral"
    LNil (LCons (SyntaxTerminal "<<") LNil) EvalNone)
  (LCons (GrammarRule "syntax-bnf:grammar-literal-type-exists" "SyntaxBnfGrammarLiteral"
    LNil (LCons (SyntaxTerminal "?*") LNil) EvalNone)
  (LCons (GrammarRule "syntax-bnf:grammar-literal-forward-imp" "SyntaxBnfGrammarLiteral"
    LNil (LCons (SyntaxTerminal "=>") LNil) EvalNone)
  (LCons (GrammarRule "syntax-bnf:grammar-literal-gentzen" "SyntaxBnfGrammarLiteral"
    LNil (LCons (SyntaxTerminal "-->") LNil) EvalNone)
  (LCons (GrammarRule "syntax-bnf:grammar-literal-type-forall" "SyntaxBnfGrammarLiteral"
    LNil (LCons (SyntaxTerminal "!>") LNil) EvalNone)
  (LCons (GrammarRule "syntax-bnf:grammar-layout-horizontal" "SyntaxBnfGrammarLayout"
    (LCons (TermSimple "space" (TBase "SyntaxBnfHorizontalSpace")) LNil)
    (LCons (SyntaxNonTerminal "space") LNil) EvalNone)
  (LCons (GrammarRule "syntax-bnf:grammar-layout-continuation" "SyntaxBnfGrammarLayout"
    (LCons (TermSimple "continuation" (TBase "SyntaxBnfGrammarContinuation")) LNil)
    (LCons (SyntaxNonTerminal "continuation") LNil) EvalNone)
  (LCons (GrammarRule "syntax-bnf:grammar-continuation" "SyntaxBnfGrammarContinuation"
    (LCons (TermSimple "end" (TBase "SyntaxBnfLineEnd"))
    (LCons (TermSimple "first-indent" (TBase "SyntaxBnfHorizontalSpace")) LNil))
    (LCons (SyntaxNonTerminal "end")
    (LCons (SyntaxNonTerminal "first-indent") LNil)) EvalNone)

  (LCons (GrammarRule "syntax-bnf:regex-expression" "SyntaxBnfRegexExpression"
    (LCons (TermSimple "first" (TBase "SyntaxBnfRegexSequence"))
    (LCons (TermSimple "tail" (TBase "SyntaxBnfRegexAlternativeTail")) LNil))
    (LCons (SyntaxNonTerminal "first")
    (LCons (SyntaxNonTerminal "tail") LNil)) EvalNone)
  (LCons (GrammarRule "syntax-bnf:regex-alternatives-empty" "SyntaxBnfRegexAlternativeTail"
    LNil LNil EvalNone)
  (LCons (GrammarRule "syntax-bnf:regex-alternatives-cons" "SyntaxBnfRegexAlternativeTail"
    (LCons (TermSimple "next" (TBase "SyntaxBnfRegexSequence"))
    (LCons (TermSimple "tail" (TBase "SyntaxBnfRegexAlternativeTail")) LNil))
    (LCons (SyntaxTerminal "|")
    (LCons (SyntaxNonTerminal "next")
    (LCons (SyntaxNonTerminal "tail") LNil))) EvalNone)
  (LCons (GrammarRule "syntax-bnf:regex-sequence-empty" "SyntaxBnfRegexSequence" LNil LNil EvalNone)
  (LCons (GrammarRule "syntax-bnf:regex-sequence-cons" "SyntaxBnfRegexSequence"
    (LCons (TermSimple "head" (TBase "SyntaxBnfRegexElement"))
    (LCons (TermSimple "tail" (TBase "SyntaxBnfRegexSequence")) LNil))
    (LCons (SyntaxNonTerminal "head")
    (LCons (SyntaxNonTerminal "tail") LNil)) EvalNone)
  (LCons (GrammarRule "syntax-bnf:regex-element-piece" "SyntaxBnfRegexElement"
    (LCons (TermSimple "piece" (TBase "SyntaxBnfRegexPiece")) LNil)
    (LCons (SyntaxNonTerminal "piece") LNil) EvalNone)
  (LCons (GrammarRule "syntax-bnf:regex-element-layout" "SyntaxBnfRegexElement"
    (LCons (TermSimple "layout" (TBase "SyntaxBnfRegexLayout")) LNil)
    (LCons (SyntaxNonTerminal "layout") LNil) EvalNone)
  (LCons (GrammarRule "syntax-bnf:regex-piece" "SyntaxBnfRegexPiece"
    (LCons (TermSimple "atom" (TBase "SyntaxBnfRegexAtom"))
    (LCons (TermSimple "repetition" (TBase "SyntaxBnfRegexRepetition")) LNil))
    (LCons (SyntaxNonTerminal "atom")
    (LCons (SyntaxNonTerminal "repetition") LNil)) EvalNone)
  (LCons (GrammarRule "syntax-bnf:regex-atom-angle-reference" "SyntaxBnfRegexAtom"
    (LCons (TermSimple "name" (TBase "SyntaxBnfRuleName")) LNil)
    (LCons (SyntaxTerminal "<")
    (LCons (SyntaxNonTerminal "name")
    (LCons (SyntaxTerminal ">") LNil))) EvalNone)
  (LCons (GrammarRule "syntax-bnf:regex-atom-brace-reference" "SyntaxBnfRegexAtom"
    (LCons (TermSimple "name" (TBase "SyntaxBnfRuleName")) LNil)
    (LCons (SyntaxTerminal "{")
    (LCons (SyntaxNonTerminal "name")
    (LCons (SyntaxTerminal "}") LNil))) EvalNone)
  (LCons (GrammarRule "syntax-bnf:regex-atom-group" "SyntaxBnfRegexAtom"
    (LCons (TermSimple "body" (TBase "SyntaxBnfRegexExpression")) LNil)
    (LCons (SyntaxTerminal "(")
    (LCons (SyntaxNonTerminal "body")
    (LCons (SyntaxTerminal ")") LNil))) EvalNone)
  (LCons (GrammarRule "syntax-bnf:regex-atom-class" "SyntaxBnfRegexAtom"
    (LCons (TermSimple "class" (TBase "SyntaxBnfRegexClass")) LNil)
    (LCons (SyntaxNonTerminal "class") LNil) EvalNone)
  (LCons (GrammarRule "syntax-bnf:regex-atom-wildcard" "SyntaxBnfRegexAtom"
    LNil (LCons (SyntaxTerminal ".") LNil) EvalNone)
  (LCons (GrammarRule "syntax-bnf:regex-repetition-one" "SyntaxBnfRegexRepetition"
    LNil LNil EvalNone)
  (LCons (GrammarRule "syntax-bnf:regex-repetition-zero-or-more" "SyntaxBnfRegexRepetition"
    LNil (LCons (SyntaxTerminal "*") LNil) EvalNone)
  (LCons (GrammarRule "syntax-bnf:regex-layout-horizontal" "SyntaxBnfRegexLayout"
    (LCons (TermSimple "space" (TBase "SyntaxBnfHorizontalSpace")) LNil)
    (LCons (SyntaxNonTerminal "space") LNil) EvalNone)
  (LCons (GrammarRule "syntax-bnf:regex-class" "SyntaxBnfRegexClass"
    (LCons (TermSimple "negation" (TBase "SyntaxBnfRegexClassNegation"))
    (LCons (TermSimple "body" (TBase "SyntaxBnfRegexClassBody")) LNil))
    (LCons (SyntaxTerminal "[")
    (LCons (SyntaxNonTerminal "negation")
    (LCons (SyntaxNonTerminal "body")
    (LCons (SyntaxTerminal "]") LNil)))) EvalNone)
  (LCons (GrammarRule "syntax-bnf:regex-class-positive" "SyntaxBnfRegexClassNegation"
    LNil LNil EvalNone)
  (LCons (GrammarRule "syntax-bnf:regex-class-negated" "SyntaxBnfRegexClassNegation"
    LNil (LCons (SyntaxTerminal "^") LNil) EvalNone)
  (LCons (GrammarRule "syntax-bnf:regex-class-body-empty" "SyntaxBnfRegexClassBody"
    LNil LNil EvalNone)
  (LCons (GrammarRule "syntax-bnf:regex-class-body-cons" "SyntaxBnfRegexClassBody"
    (LCons (TermSimple "head" (TBase "SyntaxBnfRegexClassItem"))
    (LCons (TermSimple "tail" (TBase "SyntaxBnfRegexClassBody")) LNil))
    (LCons (SyntaxNonTerminal "head")
    (LCons (SyntaxNonTerminal "tail") LNil)) EvalNone)
  (LCons (GrammarRule "syntax-bnf:regex-class-body-final-hyphen" "SyntaxBnfRegexClassBody"
    LNil (LCons (SyntaxTerminal "-") LNil) EvalNone)
  (LCons (GrammarRule "syntax-bnf:regex-class-item-singleton" "SyntaxBnfRegexClassItem"
    (LCons (TermSimple "value" (TBase "SyntaxBnfRegexClassEndpoint")) LNil)
    (LCons (SyntaxNonTerminal "value") LNil) EvalNone)
  (LCons (GrammarRule "syntax-bnf:regex-class-item-range" "SyntaxBnfRegexClassItem"
    (LCons (TermSimple "first" (TBase "SyntaxBnfRegexClassEndpoint"))
    (LCons (TermSimple "last" (TBase "SyntaxBnfRegexClassEndpoint")) LNil))
    (LCons (SyntaxNonTerminal "first")
    (LCons (SyntaxTerminal "-")
    (LCons (SyntaxNonTerminal "last") LNil))) EvalNone)
  (LCons (GrammarRule "syntax-bnf:regex-class-endpoint-scalar" "SyntaxBnfRegexClassEndpoint"
    (LCons (TermSimple "scalar" (TBase "SyntaxBnfRegexClassScalar")) LNil)
    (LCons (SyntaxNonTerminal "scalar") LNil) EvalNone)
  (LCons (GrammarRule "syntax-bnf:regex-class-endpoint-escape" "SyntaxBnfRegexClassEndpoint"
    (LCons (TermSimple "escape" (TBase "SyntaxBnfRegexClassEscape")) LNil)
    (LCons (SyntaxNonTerminal "escape") LNil) EvalNone)
  (LCons (GrammarRule "syntax-bnf:regex-class-escape-simple" "SyntaxBnfRegexClassEscape"
    (LCons (TermSimple "scalar" (TBase "SyntaxBnfRegexSimpleEscapeScalar")) LNil)
    (LCons (SyntaxTerminal "\\")
    (LCons (SyntaxNonTerminal "scalar") LNil)) EvalNone)
  (LCons (GrammarRule "syntax-bnf:regex-class-escape-octal-three" "SyntaxBnfRegexClassEscape"
    (LCons (TermSimple "hundreds" (TBase "SyntaxBnfOctalLeadingThree"))
    (LCons (TermSimple "tens" (TBase "SyntaxBnfOctalDigit"))
    (LCons (TermSimple "ones" (TBase "SyntaxBnfOctalDigit")) LNil)))
    (LCons (SyntaxTerminal "\\")
    (LCons (SyntaxNonTerminal "hundreds")
    (LCons (SyntaxNonTerminal "tens")
    (LCons (SyntaxNonTerminal "ones") LNil)))) EvalNone)
  (LCons (GrammarRule "syntax-bnf:regex-class-escape-octal-two" "SyntaxBnfRegexClassEscape"
    (LCons (TermSimple "tens" (TBase "SyntaxBnfOctalLeadingTwo"))
    (LCons (TermSimple "ones" (TBase "SyntaxBnfOctalDigit")) LNil))
    (LCons (SyntaxTerminal "\\")
    (LCons (SyntaxNonTerminal "tens")
    (LCons (SyntaxNonTerminal "ones") LNil))) EvalNone)

  LNil)))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))
  LNil
  LNil)
