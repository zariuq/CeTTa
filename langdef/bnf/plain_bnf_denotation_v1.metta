(gslt-presentation-v1 PlainBnfDenotationV1
  (signature
    (operator metta-equation 2)
    (operator langdef:codepoints->string 1)
    (operator langdef:string->codepoints 1)
    (operator cp 1)
    (operator cons 2)

    (operator bnf-v1:denote-candidate 2)
    (operator bnf-v1:denote-types 2)
    (operator bnf-v1:denote-lexical-types 1)
    (operator bnf-v1:denote-rules 1)
    (operator bnf-v1:denote-alternatives 4)
    (operator bnf-v1:denote-rule 3)
    (operator bnf-v1:denote-params 2)
    (operator bnf-v1:denote-syntax 2)
    (operator bnf-v1:denote-origins 1)
    (operator bnf-v1:denote-origin-alternatives 5)
    (operator bnf-v1:denote-lexical-classes 1)
    (operator bnf-v1:denote-lexical-class 2)
    (operator bnf-v1:denote-lexical-states 1)
    (operator bnf-v1:denote-scalars 1)
    (operator bnf-v1:denote-lexical-origins 1)
    (operator bnf-v1:text->codepoints 1)
    (operator bnf-v1:codepoints->text 1)
    (operator bnf-v1:string->text 1)
    (operator bnf-v1:text->string 1)
    (operator bnf-v1:text-append 2)
    (operator bnf-v1:suffix-start 0)
    (operator bnf-v1:suffix-next 1)
    (operator bnf-v1:param-start 0)

    (operator bnf-v1:text-nil 0)
    (operator bnf-v1:text-cons 2)
    (operator bnf-v1:source-span 2)
    (operator bnf-v1:document 2)
    (operator bnf-v1:entries-nil 0)
    (operator bnf-v1:entries-cons 2)
    (operator bnf-v1:rule 3)
    (operator bnf-v1:comment 2)
    (operator bnf-v1:blank 1)
    (operator bnf-v1:expression 2)
    (operator bnf-v1:alternatives-nil 0)
    (operator bnf-v1:alternatives-cons 2)
    (operator bnf-v1:alternative 2)
    (operator bnf-v1:elements-nil 0)
    (operator bnf-v1:elements-cons 2)
    (operator bnf-v1:reference 2)
    (operator bnf-v1:literal 2)
    (operator bnf-v1:lexical-declarations-nil 0)
    (operator bnf-v1:lexical-declarations-cons 2)
    (operator bnf-v1:lexical-declaration 5)
    (operator bnf-v1:lexical-points 1)
    (operator bnf-v1:lexical-except 1)
    (operator bnf-v1:scalars-nil 0)
    (operator bnf-v1:scalars-cons 2)
    (operator bnf-v1:lexical-origin 2)

    (operator BNF:DenotationCandidate 3)
    (operator BNF:DenotationOriginsV1 2)
    (operator BNF:GeneratedRuleOrigin 4)
    (operator BNF:GeneratedLexicalOrigin 5)
    (operator BNFSemanticValidationAcceptedV1 4)
    (operator BNFStartSomeV1 2)
    (operator GSLTLanguageDefWireV1 5)
    (operator GSLTParserProfileLayerV1 4)
    (operator LNil 0)
    (operator LCons 2)
    (operator TypeDecl 2)
    (operator CarrierAst 0)
    (operator GrammarRule 5)
    (operator TermSimple 2)
    (operator TBase 1)
    (operator SyntaxTerminal 1)
    (operator SyntaxNonTerminal 1)
    (operator LexicalClassPoints 2)
    (operator LexicalClassExcept 2)
    (operator LexicalState 3)
    (operator EvalNone 0))
  (equations)
  (rewrites
    (rule denote-candidate
      (head (metta-equation
        (bnf-v1:denote-candidate
          (bnf-v1:document ?entries ?document-span)
          (BNFSemanticValidationAcceptedV1
            (BNFStartSomeV1 ?start-name ?start-span)
            ?definitions ?lexicals ?analysis))
        (BNF:DenotationCandidate
          (GSLTLanguageDefWireV1
            "PlainBnfDenotedSyntaxV1"
            (bnf-v1:denote-types
              ?entries (bnf-v1:denote-lexical-types ?lexicals))
            (bnf-v1:denote-rules ?entries)
            LNil
            LNil)
          (GSLTParserProfileLayerV1
            "PlainBnfDenotedParserV1"
            (langdef:codepoints->string
              (bnf-v1:text->codepoints ?start-name))
            (bnf-v1:denote-lexical-classes ?lexicals)
            (bnf-v1:denote-lexical-states ?lexicals))
          (BNF:DenotationOriginsV1
            (bnf-v1:denote-origins ?entries)
            (bnf-v1:denote-lexical-origins ?lexicals)))))
      (body))

    (rule denote-types-empty
      (head (metta-equation
        (bnf-v1:denote-types (bnf-v1:entries-nil) ?tail)
        ?tail))
      (body))
    (rule denote-types-rule
      (head (metta-equation
        (bnf-v1:denote-types
          (bnf-v1:entries-cons
            (bnf-v1:rule ?name ?expression ?span) ?rest)
          ?tail)
        (LCons
          (TypeDecl
            (langdef:codepoints->string
              (bnf-v1:text->codepoints ?name))
            CarrierAst)
          (bnf-v1:denote-types ?rest ?tail))))
      (body))
    (rule denote-types-comment
      (head (metta-equation
        (bnf-v1:denote-types
          (bnf-v1:entries-cons
            (bnf-v1:comment ?text ?span) ?rest)
          ?tail)
        (bnf-v1:denote-types ?rest ?tail)))
      (body))
    (rule denote-types-blank
      (head (metta-equation
        (bnf-v1:denote-types
          (bnf-v1:entries-cons
            (bnf-v1:blank ?span) ?rest)
          ?tail)
        (bnf-v1:denote-types ?rest ?tail)))
      (body))

    (rule denote-lexical-types-empty
      (head (metta-equation
        (bnf-v1:denote-lexical-types
          (bnf-v1:lexical-declarations-nil))
        LNil))
      (body))
    (rule denote-lexical-types-cons
      (head (metta-equation
        (bnf-v1:denote-lexical-types
          (bnf-v1:lexical-declarations-cons
            (bnf-v1:lexical-declaration
              ?name ?class ?matcher ?label ?origin)
            ?tail))
        (LCons
          (TypeDecl
            (langdef:codepoints->string
              (bnf-v1:text->codepoints ?name))
            CarrierAst)
          (bnf-v1:denote-lexical-types ?tail))))
      (body))

    (rule denote-rules-empty
      (head (metta-equation
        (bnf-v1:denote-rules (bnf-v1:entries-nil))
        LNil))
      (body))
    (rule denote-rules-rule
      (head (metta-equation
        (bnf-v1:denote-rules
          (bnf-v1:entries-cons
            (bnf-v1:rule ?name
              (bnf-v1:expression ?alternatives ?expression-span)
              ?rule-span)
            ?tail))
        (bnf-v1:denote-alternatives
          ?name ?alternatives (bnf-v1:suffix-start)
          (bnf-v1:denote-rules ?tail))))
      (body))
    (rule denote-rules-comment
      (head (metta-equation
        (bnf-v1:denote-rules
          (bnf-v1:entries-cons
            (bnf-v1:comment ?text ?span) ?tail))
        (bnf-v1:denote-rules ?tail)))
      (body))
    (rule denote-rules-blank
      (head (metta-equation
        (bnf-v1:denote-rules
          (bnf-v1:entries-cons
            (bnf-v1:blank ?span) ?tail))
        (bnf-v1:denote-rules ?tail)))
      (body))

    (rule denote-alternatives-empty
      (head (metta-equation
        (bnf-v1:denote-alternatives
          ?name (bnf-v1:alternatives-nil) ?suffix ?tail)
        ?tail))
      (body))
    (rule denote-alternatives-cons
      (head (metta-equation
        (bnf-v1:denote-alternatives
          ?name
          (bnf-v1:alternatives-cons ?alternative ?rest)
          ?suffix ?tail)
        (LCons
          (bnf-v1:denote-rule ?name ?alternative ?suffix)
          (bnf-v1:denote-alternatives
            ?name ?rest (bnf-v1:suffix-next ?suffix) ?tail))))
      (body))
    (rule denote-rule
      (head (metta-equation
        (bnf-v1:denote-rule
          ?name (bnf-v1:alternative ?elements ?span) ?suffix)
        (GrammarRule
          (langdef:codepoints->string
            (bnf-v1:text->codepoints
              (bnf-v1:text-append ?name ?suffix)))
          (langdef:codepoints->string
            (bnf-v1:text->codepoints ?name))
          (bnf-v1:denote-params ?elements (bnf-v1:param-start))
          (bnf-v1:denote-syntax ?elements (bnf-v1:param-start))
          EvalNone)))
      (body))

    (rule denote-params-empty
      (head (metta-equation
        (bnf-v1:denote-params (bnf-v1:elements-nil) ?suffix)
        LNil))
      (body))
    (rule denote-params-reference
      (head (metta-equation
        (bnf-v1:denote-params
          (bnf-v1:elements-cons
            (bnf-v1:reference ?name ?span) ?tail)
          ?suffix)
        (LCons
          (TermSimple
            (langdef:codepoints->string
              (bnf-v1:text->codepoints ?suffix))
            (TBase
              (langdef:codepoints->string
                (bnf-v1:text->codepoints ?name))))
          (bnf-v1:denote-params
            ?tail (bnf-v1:suffix-next ?suffix)))))
      (body))
    (rule denote-params-literal
      (head (metta-equation
        (bnf-v1:denote-params
          (bnf-v1:elements-cons
            (bnf-v1:literal ?text ?span) ?tail)
          ?suffix)
        (bnf-v1:denote-params ?tail ?suffix)))
      (body))

    (rule denote-syntax-empty
      (head (metta-equation
        (bnf-v1:denote-syntax (bnf-v1:elements-nil) ?suffix)
        LNil))
      (body))
    (rule denote-syntax-reference
      (head (metta-equation
        (bnf-v1:denote-syntax
          (bnf-v1:elements-cons
            (bnf-v1:reference ?name ?span) ?tail)
          ?suffix)
        (LCons
          (SyntaxNonTerminal
            (langdef:codepoints->string
              (bnf-v1:text->codepoints ?suffix)))
          (bnf-v1:denote-syntax
            ?tail (bnf-v1:suffix-next ?suffix)))))
      (body))
    (rule denote-syntax-empty-literal
      (head (metta-equation
        (bnf-v1:denote-syntax
          (bnf-v1:elements-cons
            (bnf-v1:literal (bnf-v1:text-nil) ?span) ?tail)
          ?suffix)
        (bnf-v1:denote-syntax ?tail ?suffix)))
      (body))
    (rule denote-syntax-literal
      (head (metta-equation
        (bnf-v1:denote-syntax
          (bnf-v1:elements-cons
            (bnf-v1:literal
              (bnf-v1:text-cons ?scalar ?text-tail) ?span)
            ?tail)
          ?suffix)
        (LCons
          (SyntaxTerminal
            (langdef:codepoints->string
              (bnf-v1:text->codepoints
                (bnf-v1:text-cons ?scalar ?text-tail))))
          (bnf-v1:denote-syntax ?tail ?suffix))))
      (body))

    (rule denote-origins-empty
      (head (metta-equation
        (bnf-v1:denote-origins (bnf-v1:entries-nil))
        LNil))
      (body))
    (rule denote-origins-rule
      (head (metta-equation
        (bnf-v1:denote-origins
          (bnf-v1:entries-cons
            (bnf-v1:rule ?name
              (bnf-v1:expression ?alternatives ?expression-span)
              ?rule-span)
            ?tail))
        (bnf-v1:denote-origin-alternatives
          ?name ?rule-span ?alternatives (bnf-v1:suffix-start)
          (bnf-v1:denote-origins ?tail))))
      (body))
    (rule denote-origins-comment
      (head (metta-equation
        (bnf-v1:denote-origins
          (bnf-v1:entries-cons
            (bnf-v1:comment ?text ?span) ?tail))
        (bnf-v1:denote-origins ?tail)))
      (body))
    (rule denote-origins-blank
      (head (metta-equation
        (bnf-v1:denote-origins
          (bnf-v1:entries-cons
            (bnf-v1:blank ?span) ?tail))
        (bnf-v1:denote-origins ?tail)))
      (body))
    (rule denote-origin-alternatives-empty
      (head (metta-equation
        (bnf-v1:denote-origin-alternatives
          ?name ?rule-span (bnf-v1:alternatives-nil) ?suffix ?tail)
        ?tail))
      (body))
    (rule denote-origin-alternatives-cons
      (head (metta-equation
        (bnf-v1:denote-origin-alternatives
          ?name ?rule-span
          (bnf-v1:alternatives-cons
            (bnf-v1:alternative ?elements ?alternative-span) ?rest)
          ?suffix ?tail)
        (LCons
          (BNF:GeneratedRuleOrigin
            (langdef:codepoints->string
              (bnf-v1:text->codepoints
                (bnf-v1:text-append ?name ?suffix)))
            (langdef:codepoints->string
              (bnf-v1:text->codepoints ?name))
            ?rule-span
            ?alternative-span)
          (bnf-v1:denote-origin-alternatives
            ?name ?rule-span ?rest (bnf-v1:suffix-next ?suffix) ?tail))))
      (body))

    ; The lexical context is denoted structurally.  The source reference name
    ; is also the generated sort name, so no unproved renaming table sits
    ; between grammar references and lexical states.
    (rule denote-lexical-classes-empty
      (head (metta-equation
        (bnf-v1:denote-lexical-classes
          (bnf-v1:lexical-declarations-nil))
        LNil))
      (body))
    (rule denote-lexical-classes-cons
      (head (metta-equation
        (bnf-v1:denote-lexical-classes
          (bnf-v1:lexical-declarations-cons
            (bnf-v1:lexical-declaration
              ?name ?class ?matcher ?label ?origin)
            ?tail))
        (LCons
          (bnf-v1:denote-lexical-class ?class ?matcher)
          (bnf-v1:denote-lexical-classes ?tail))))
      (body))
    (rule denote-lexical-class-points
      (head (metta-equation
        (bnf-v1:denote-lexical-class
          ?class (bnf-v1:lexical-points ?scalars))
        (LexicalClassPoints ?class (bnf-v1:denote-scalars ?scalars))))
      (body))
    (rule denote-lexical-class-except
      (head (metta-equation
        (bnf-v1:denote-lexical-class
          ?class (bnf-v1:lexical-except ?scalars))
        (LexicalClassExcept ?class (bnf-v1:denote-scalars ?scalars))))
      (body))

    (rule denote-lexical-states-empty
      (head (metta-equation
        (bnf-v1:denote-lexical-states
          (bnf-v1:lexical-declarations-nil))
        LNil))
      (body))
    (rule denote-lexical-states-cons
      (head (metta-equation
        (bnf-v1:denote-lexical-states
          (bnf-v1:lexical-declarations-cons
            (bnf-v1:lexical-declaration
              ?name ?class ?matcher ?label ?origin)
            ?tail))
        (LCons
          (LexicalState
            (langdef:codepoints->string
              (bnf-v1:text->codepoints ?name))
            ?class ?label)
          (bnf-v1:denote-lexical-states ?tail))))
      (body))

    (rule denote-scalars-empty
      (head (metta-equation
        (bnf-v1:denote-scalars (bnf-v1:scalars-nil))
        LNil))
      (body))
    (rule denote-scalars-cons
      (head (metta-equation
        (bnf-v1:denote-scalars
          (bnf-v1:scalars-cons ?head ?tail))
        (LCons ?head (bnf-v1:denote-scalars ?tail))))
      (body))

    (rule denote-lexical-origins-empty
      (head (metta-equation
        (bnf-v1:denote-lexical-origins
          (bnf-v1:lexical-declarations-nil))
        LNil))
      (body))
    (rule denote-lexical-origins-cons
      (head (metta-equation
        (bnf-v1:denote-lexical-origins
          (bnf-v1:lexical-declarations-cons
            (bnf-v1:lexical-declaration
              ?name ?class ?matcher ?label ?origin)
            ?tail))
        (LCons
          (BNF:GeneratedLexicalOrigin
            (langdef:codepoints->string
              (bnf-v1:text->codepoints ?name))
            ?class ?label ?matcher ?origin)
          (bnf-v1:denote-lexical-origins ?tail))))
      (body))

    (rule text-to-codepoints-empty
      (head (metta-equation
        (bnf-v1:text->codepoints (bnf-v1:text-nil))
        nil))
      (body))
    (rule text-to-codepoints-cons
      (head (metta-equation
        (bnf-v1:text->codepoints
          (bnf-v1:text-cons ?head ?tail))
        (cons (cp ?head) (bnf-v1:text->codepoints ?tail))))
      (body))
    (rule text-append-empty
      (head (metta-equation
        (bnf-v1:text-append (bnf-v1:text-nil) ?right)
        ?right))
      (body))
    (rule text-append-cons
      (head (metta-equation
        (bnf-v1:text-append
          (bnf-v1:text-cons ?head ?tail) ?right)
        (bnf-v1:text-cons
          ?head (bnf-v1:text-append ?tail ?right))))
      (body))
    (rule suffix-start
      (head (metta-equation
        (bnf-v1:suffix-start)
        (bnf-v1:text-cons 35 (bnf-v1:text-nil))))
      (body))
    (rule suffix-next
      (head (metta-equation
        (bnf-v1:suffix-next ?suffix)
        (bnf-v1:text-append
          ?suffix (bnf-v1:text-cons 120 (bnf-v1:text-nil)))))
      (body))
    (rule param-start
      (head (metta-equation
        (bnf-v1:param-start)
        (bnf-v1:text-cons 112
          (bnf-v1:text-cons 35 (bnf-v1:text-nil)))))
      (body))
    (rule codepoints-to-text-empty
      (head (metta-equation
        (bnf-v1:codepoints->text nil)
        (bnf-v1:text-nil)))
      (body))
    (rule codepoints-to-text-cons
      (head (metta-equation
        (bnf-v1:codepoints->text (cons (cp ?head) ?tail))
        (bnf-v1:text-cons ?head (bnf-v1:codepoints->text ?tail))))
      (body))
    (rule string-to-text
      (head (metta-equation
        (bnf-v1:string->text ?string)
        (bnf-v1:codepoints->text (langdef:string->codepoints ?string))))
      (body))
    (rule text-to-string
      (head (metta-equation
        (bnf-v1:text->string ?text)
        (langdef:codepoints->string (bnf-v1:text->codepoints ?text))))
      (body))))
