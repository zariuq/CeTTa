(gslt-presentation-v1 TptpOfficialExtendedBnfAstProjectionV1
  (signature
    (operator metta-equation 2)
    (operator CstRuleV1 3)
    (operator CstRuleV1 4)
    (operator CstRuleV1 5)
    (operator CstRuleV1 6)
    (operator CstRuleV1 7)
    (operator cp 1)

    (operator tptp-ebnf-v1:project 1)
    (operator tptp-ebnf-v1:project-lines 1)
    (operator tptp-ebnf-v1:project-line-onto 2)
    (operator tptp-ebnf-v1:project-row 1)
    (operator tptp-ebnf-v1:project-name 1)
    (operator tptp-ebnf-v1:project-name-tail 1)
    (operator tptp-ebnf-v1:project-name-char 1)
    (operator tptp-ebnf-v1:project-comment 1)
    (operator tptp-ebnf-v1:project-comment-chars 1)
    (operator tptp-ebnf-v1:project-line-scalar 1)
    (operator tptp-ebnf-v1:project-grammar-expression 1)
    (operator tptp-ebnf-v1:project-grammar-alternatives 1)
    (operator tptp-ebnf-v1:project-grammar-sequence 1)
    (operator tptp-ebnf-v1:project-grammar-elements 1)
    (operator tptp-ebnf-v1:project-grammar-element-onto 2)
    (operator tptp-ebnf-v1:project-grammar-reference 1)
    (operator tptp-ebnf-v1:project-grammar-repetition 1)
    (operator tptp-ebnf-v1:project-grammar-literal 1)
    (operator tptp-ebnf-v1:project-grammar-scalar 1)
    (operator tptp-ebnf-v1:project-regex-expression 1)
    (operator tptp-ebnf-v1:project-regex-alternatives 1)
    (operator tptp-ebnf-v1:project-regex-sequence 1)
    (operator tptp-ebnf-v1:project-regex-elements 1)
    (operator tptp-ebnf-v1:project-regex-element-onto 2)
    (operator tptp-ebnf-v1:project-regex-piece 1)
    (operator tptp-ebnf-v1:project-regex-atom 1)
    (operator tptp-ebnf-v1:project-regex-repetition 1)
    (operator tptp-ebnf-v1:project-regex-class 1)
    (operator tptp-ebnf-v1:project-regex-polarity 1)
    (operator tptp-ebnf-v1:project-regex-class-items 1)
    (operator tptp-ebnf-v1:project-regex-class-item 1)
    (operator tptp-ebnf-v1:project-regex-endpoint 1)
    (operator tptp-ebnf-v1:project-regex-escape 1)
    (operator tptp-ebnf-v1:project-regex-class-scalar 1)
    (operator tptp-ebnf-v1:project-regex-simple-escape-scalar 1)
    (operator tptp-ebnf-v1:project-octal-leading-three 1)
    (operator tptp-ebnf-v1:project-octal-leading-two 1)
    (operator tptp-ebnf-v1:project-octal-digit 1)

    (operator tptp-ebnf-v1:text-nil 0)
    (operator tptp-ebnf-v1:text-cons 2)
    (operator tptp-ebnf-v1:source-span 2)
    (operator tptp-ebnf-v1:document 2)
    (operator tptp-ebnf-v1:entries-nil 0)
    (operator tptp-ebnf-v1:entries-cons 2)
    (operator tptp-ebnf-v1:syntax-row 3)
    (operator tptp-ebnf-v1:semantic-row 3)
    (operator tptp-ebnf-v1:token-row 3)
    (operator tptp-ebnf-v1:character-row 3)
    (operator tptp-ebnf-v1:comment 2)
    (operator tptp-ebnf-v1:blank 1)
    (operator tptp-ebnf-v1:grammar-choice 2)
    (operator tptp-ebnf-v1:grammar-sequences-nil 0)
    (operator tptp-ebnf-v1:grammar-sequences-cons 2)
    (operator tptp-ebnf-v1:grammar-sequence 2)
    (operator tptp-ebnf-v1:grammar-elements-nil 0)
    (operator tptp-ebnf-v1:grammar-elements-cons 2)
    (operator tptp-ebnf-v1:grammar-reference 3)
    (operator tptp-ebnf-v1:grammar-literal 2)
    (operator tptp-ebnf-v1:grammar-once 0)
    (operator tptp-ebnf-v1:grammar-zero-or-more 0)
    (operator tptp-ebnf-v1:regex-choice 2)
    (operator tptp-ebnf-v1:regex-sequences-nil 0)
    (operator tptp-ebnf-v1:regex-sequences-cons 2)
    (operator tptp-ebnf-v1:regex-sequence 2)
    (operator tptp-ebnf-v1:regex-elements-nil 0)
    (operator tptp-ebnf-v1:regex-elements-cons 2)
    (operator tptp-ebnf-v1:regex-piece 3)
    (operator tptp-ebnf-v1:regex-angle-reference 2)
    (operator tptp-ebnf-v1:regex-brace-reference 2)
    (operator tptp-ebnf-v1:regex-group 2)
    (operator tptp-ebnf-v1:regex-class 3)
    (operator tptp-ebnf-v1:regex-wildcard 1)
    (operator tptp-ebnf-v1:regex-once 0)
    (operator tptp-ebnf-v1:regex-zero-or-more 0)
    (operator tptp-ebnf-v1:regex-class-positive 0)
    (operator tptp-ebnf-v1:regex-class-negated 0)
    (operator tptp-ebnf-v1:regex-class-items-nil 0)
    (operator tptp-ebnf-v1:regex-class-items-cons 2)
    (operator tptp-ebnf-v1:regex-class-singleton 2)
    (operator tptp-ebnf-v1:regex-class-range 3)
    (operator tptp-ebnf-v1:regex-endpoint-scalar 2)
    (operator tptp-ebnf-v1:regex-endpoint-simple-escape 2)
    (operator tptp-ebnf-v1:regex-endpoint-octal-escape 2))
  (equations)
  (rewrites
    (rule project-document
      (head (metta-equation
        (tptp-ebnf-v1:project
          (CstRuleV1 "syntax-bnf:document" ?start ?stop ?lines))
        (tptp-ebnf-v1:document
          (tptp-ebnf-v1:project-lines ?lines)
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))

    (rule project-lines-empty
      (head (metta-equation
        (tptp-ebnf-v1:project-lines
          (CstRuleV1 "syntax-bnf:lines-empty" ?start ?stop))
        (tptp-ebnf-v1:entries-nil)))
      (body))
    (rule project-lines-cons
      (head (metta-equation
        (tptp-ebnf-v1:project-lines
          (CstRuleV1 "syntax-bnf:lines-cons" ?start ?stop ?head ?tail))
        (tptp-ebnf-v1:project-line-onto
          ?head (tptp-ebnf-v1:project-lines ?tail))))
      (body))
    (rule project-line-comment
      (head (metta-equation
        (tptp-ebnf-v1:project-line-onto
          (CstRuleV1 "syntax-bnf:line-comment" ?start ?stop ?comment)
          ?tail)
        (tptp-ebnf-v1:entries-cons
          (tptp-ebnf-v1:project-comment ?comment) ?tail)))
      (body))
    (rule project-line-blank
      (head (metta-equation
        (tptp-ebnf-v1:project-line-onto
          (CstRuleV1 "syntax-bnf:line-blank" ?start ?stop ?blank)
          ?tail)
        (tptp-ebnf-v1:entries-cons
          (tptp-ebnf-v1:blank
            (tptp-ebnf-v1:source-span ?start ?stop))
          ?tail)))
      (body))
    (rule project-line-row
      (head (metta-equation
        (tptp-ebnf-v1:project-line-onto
          (CstRuleV1 "syntax-bnf:line-row" ?start ?stop ?row)
          ?tail)
        (tptp-ebnf-v1:entries-cons
          (tptp-ebnf-v1:project-row ?row) ?tail)))
      (body))

    (rule project-comment
      (head (metta-equation
        (tptp-ebnf-v1:project-comment
          (CstRuleV1 "syntax-bnf:comment" ?start ?stop ?body ?end))
        (tptp-ebnf-v1:comment
          (tptp-ebnf-v1:project-comment-chars ?body)
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-comment-chars-empty
      (head (metta-equation
        (tptp-ebnf-v1:project-comment-chars
          (CstRuleV1 "syntax-bnf:comment-chars-empty" ?start ?stop))
        (tptp-ebnf-v1:text-nil)))
      (body))
    (rule project-comment-chars-cons
      (head (metta-equation
        (tptp-ebnf-v1:project-comment-chars
          (CstRuleV1 "syntax-bnf:comment-chars-cons" ?start ?stop ?head ?tail))
        (tptp-ebnf-v1:text-cons
          (tptp-ebnf-v1:project-line-scalar ?head)
          (tptp-ebnf-v1:project-comment-chars ?tail))))
      (body))
    (rule project-line-scalar
      (head (metta-equation
        (tptp-ebnf-v1:project-line-scalar
          (CstRuleV1 "syntax-bnf:lex-line-scalar" ?start ?stop (cp ?scalar)))
        ?scalar))
      (body))

    (rule project-row-syntax
      (head (metta-equation
        (tptp-ebnf-v1:project-row
          (CstRuleV1 "syntax-bnf:row-syntax" ?start ?stop
            ?name ?padding ?rhs ?end))
        (tptp-ebnf-v1:syntax-row
          (tptp-ebnf-v1:project-name ?name)
          (tptp-ebnf-v1:project-grammar-expression ?rhs)
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-row-semantic
      (head (metta-equation
        (tptp-ebnf-v1:project-row
          (CstRuleV1 "syntax-bnf:row-semantic" ?start ?stop
            ?name ?padding ?rhs ?end))
        (tptp-ebnf-v1:semantic-row
          (tptp-ebnf-v1:project-name ?name)
          (tptp-ebnf-v1:project-grammar-expression ?rhs)
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-row-token
      (head (metta-equation
        (tptp-ebnf-v1:project-row
          (CstRuleV1 "syntax-bnf:row-token" ?start ?stop
            ?name ?padding ?rhs ?end))
        (tptp-ebnf-v1:token-row
          (tptp-ebnf-v1:project-name ?name)
          (tptp-ebnf-v1:project-regex-expression ?rhs)
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-row-character
      (head (metta-equation
        (tptp-ebnf-v1:project-row
          (CstRuleV1 "syntax-bnf:row-character" ?start ?stop
            ?name ?padding ?rhs ?end))
        (tptp-ebnf-v1:character-row
          (tptp-ebnf-v1:project-name ?name)
          (tptp-ebnf-v1:project-regex-expression ?rhs)
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))

    (rule project-name
      (head (metta-equation
        (tptp-ebnf-v1:project-name
          (CstRuleV1 "syntax-bnf:name" ?start ?stop ?head ?tail))
        (tptp-ebnf-v1:text-cons
          (tptp-ebnf-v1:project-name-char ?head)
          (tptp-ebnf-v1:project-name-tail ?tail))))
      (body))
    (rule project-name-tail-empty
      (head (metta-equation
        (tptp-ebnf-v1:project-name-tail
          (CstRuleV1 "syntax-bnf:name-tail-empty" ?start ?stop))
        (tptp-ebnf-v1:text-nil)))
      (body))
    (rule project-name-tail-cons
      (head (metta-equation
        (tptp-ebnf-v1:project-name-tail
          (CstRuleV1 "syntax-bnf:name-tail-cons" ?start ?stop ?head ?tail))
        (tptp-ebnf-v1:text-cons
          (tptp-ebnf-v1:project-name-char ?head)
          (tptp-ebnf-v1:project-name-tail ?tail))))
      (body))
    (rule project-name-char
      (head (metta-equation
        (tptp-ebnf-v1:project-name-char
          (CstRuleV1 "syntax-bnf:lex-name-char" ?start ?stop (cp ?scalar)))
        ?scalar))
      (body))

    (rule project-grammar-expression
      (head (metta-equation
        (tptp-ebnf-v1:project-grammar-expression
          (CstRuleV1 "syntax-bnf:grammar-expression" ?start ?stop ?first ?tail))
        (tptp-ebnf-v1:grammar-choice
          (tptp-ebnf-v1:grammar-sequences-cons
            (tptp-ebnf-v1:project-grammar-sequence ?first)
            (tptp-ebnf-v1:project-grammar-alternatives ?tail))
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-grammar-alternatives-empty
      (head (metta-equation
        (tptp-ebnf-v1:project-grammar-alternatives
          (CstRuleV1 "syntax-bnf:grammar-alternatives-empty" ?start ?stop))
        (tptp-ebnf-v1:grammar-sequences-nil)))
      (body))
    (rule project-grammar-alternatives-cons
      (head (metta-equation
        (tptp-ebnf-v1:project-grammar-alternatives
          (CstRuleV1 "syntax-bnf:grammar-alternatives-cons" ?start ?stop
            ?next ?tail))
        (tptp-ebnf-v1:grammar-sequences-cons
          (tptp-ebnf-v1:project-grammar-sequence ?next)
          (tptp-ebnf-v1:project-grammar-alternatives ?tail))))
      (body))
    (rule project-grammar-sequence
      (head (metta-equation
        (tptp-ebnf-v1:project-grammar-sequence
          (CstRuleV1 "syntax-bnf:grammar-sequence-empty" ?start ?stop))
        (tptp-ebnf-v1:grammar-sequence
          (tptp-ebnf-v1:grammar-elements-nil)
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-grammar-sequence-cons
      (head (metta-equation
        (tptp-ebnf-v1:project-grammar-sequence
          (CstRuleV1 "syntax-bnf:grammar-sequence-cons" ?start ?stop ?head ?tail))
        (tptp-ebnf-v1:grammar-sequence
          (tptp-ebnf-v1:project-grammar-elements
            (CstRuleV1 "syntax-bnf:grammar-sequence-cons" ?start ?stop ?head ?tail))
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-grammar-elements-empty
      (head (metta-equation
        (tptp-ebnf-v1:project-grammar-elements
          (CstRuleV1 "syntax-bnf:grammar-sequence-empty" ?start ?stop))
        (tptp-ebnf-v1:grammar-elements-nil)))
      (body))
    (rule project-grammar-elements-cons
      (head (metta-equation
        (tptp-ebnf-v1:project-grammar-elements
          (CstRuleV1 "syntax-bnf:grammar-sequence-cons" ?start ?stop ?head ?tail))
        (tptp-ebnf-v1:project-grammar-element-onto
          ?head (tptp-ebnf-v1:project-grammar-elements ?tail))))
      (body))
    (rule project-grammar-element-reference
      (head (metta-equation
        (tptp-ebnf-v1:project-grammar-element-onto
          (CstRuleV1 "syntax-bnf:grammar-element-reference" ?start ?stop ?reference)
          ?tail)
        (tptp-ebnf-v1:grammar-elements-cons
          (tptp-ebnf-v1:project-grammar-reference ?reference) ?tail)))
      (body))
    (rule project-grammar-element-literal
      (head (metta-equation
        (tptp-ebnf-v1:project-grammar-element-onto
          (CstRuleV1 "syntax-bnf:grammar-element-literal" ?start ?stop ?literal)
          ?tail)
        (tptp-ebnf-v1:grammar-elements-cons
          (tptp-ebnf-v1:project-grammar-literal ?literal) ?tail)))
      (body))
    (rule project-grammar-element-layout
      (head (metta-equation
        (tptp-ebnf-v1:project-grammar-element-onto
          (CstRuleV1 "syntax-bnf:grammar-element-layout" ?start ?stop ?layout)
          ?tail)
        ?tail))
      (body))
    (rule project-grammar-reference
      (head (metta-equation
        (tptp-ebnf-v1:project-grammar-reference
          (CstRuleV1 "syntax-bnf:grammar-reference" ?start ?stop ?name ?repetition))
        (tptp-ebnf-v1:grammar-reference
          (tptp-ebnf-v1:project-name ?name)
          (tptp-ebnf-v1:project-grammar-repetition ?repetition)
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-grammar-repetition-once
      (head (metta-equation
        (tptp-ebnf-v1:project-grammar-repetition
          (CstRuleV1 "syntax-bnf:grammar-repetition-one" ?start ?stop))
        (tptp-ebnf-v1:grammar-once)))
      (body))
    (rule project-grammar-repetition-star
      (head (metta-equation
        (tptp-ebnf-v1:project-grammar-repetition
          (CstRuleV1 "syntax-bnf:grammar-repetition-zero-or-more" ?start ?stop))
        (tptp-ebnf-v1:grammar-zero-or-more)))
      (body))

    (rule project-grammar-literal-scalar
      (head (metta-equation
        (tptp-ebnf-v1:project-grammar-literal
          (CstRuleV1 "syntax-bnf:grammar-literal-scalar" ?start ?stop ?scalar))
        (tptp-ebnf-v1:grammar-literal
          (tptp-ebnf-v1:text-cons
            (tptp-ebnf-v1:project-grammar-scalar ?scalar)
            (tptp-ebnf-v1:text-nil))
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-grammar-scalar
      (head (metta-equation
        (tptp-ebnf-v1:project-grammar-scalar
          (CstRuleV1 "syntax-bnf:lex-grammar-literal-scalar" ?start ?stop (cp ?scalar)))
        ?scalar))
      (body))
    (rule project-grammar-literal-equiv
      (head (metta-equation
        (tptp-ebnf-v1:project-grammar-literal
          (CstRuleV1 "syntax-bnf:grammar-literal-equiv" ?start ?stop))
        (tptp-ebnf-v1:grammar-literal
          (tptp-ebnf-v1:text-cons 60
            (tptp-ebnf-v1:text-cons 61
              (tptp-ebnf-v1:text-cons 62 (tptp-ebnf-v1:text-nil))))
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-grammar-literal-reverse-imp
      (head (metta-equation
        (tptp-ebnf-v1:project-grammar-literal
          (CstRuleV1 "syntax-bnf:grammar-literal-reverse-imp" ?start ?stop))
        (tptp-ebnf-v1:grammar-literal
          (tptp-ebnf-v1:text-cons 60
            (tptp-ebnf-v1:text-cons 61 (tptp-ebnf-v1:text-nil)))
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-grammar-literal-xor
      (head (metta-equation
        (tptp-ebnf-v1:project-grammar-literal
          (CstRuleV1 "syntax-bnf:grammar-literal-xor" ?start ?stop))
        (tptp-ebnf-v1:grammar-literal
          (tptp-ebnf-v1:text-cons 60
            (tptp-ebnf-v1:text-cons 126
              (tptp-ebnf-v1:text-cons 62 (tptp-ebnf-v1:text-nil))))
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-grammar-literal-subtype
      (head (metta-equation
        (tptp-ebnf-v1:project-grammar-literal
          (CstRuleV1 "syntax-bnf:grammar-literal-subtype" ?start ?stop))
        (tptp-ebnf-v1:grammar-literal
          (tptp-ebnf-v1:text-cons 60
            (tptp-ebnf-v1:text-cons 60 (tptp-ebnf-v1:text-nil)))
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-grammar-literal-type-exists
      (head (metta-equation
        (tptp-ebnf-v1:project-grammar-literal
          (CstRuleV1 "syntax-bnf:grammar-literal-type-exists" ?start ?stop))
        (tptp-ebnf-v1:grammar-literal
          (tptp-ebnf-v1:text-cons 63
            (tptp-ebnf-v1:text-cons 42 (tptp-ebnf-v1:text-nil)))
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-grammar-literal-forward-imp
      (head (metta-equation
        (tptp-ebnf-v1:project-grammar-literal
          (CstRuleV1 "syntax-bnf:grammar-literal-forward-imp" ?start ?stop))
        (tptp-ebnf-v1:grammar-literal
          (tptp-ebnf-v1:text-cons 61
            (tptp-ebnf-v1:text-cons 62 (tptp-ebnf-v1:text-nil)))
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-grammar-literal-gentzen
      (head (metta-equation
        (tptp-ebnf-v1:project-grammar-literal
          (CstRuleV1 "syntax-bnf:grammar-literal-gentzen" ?start ?stop))
        (tptp-ebnf-v1:grammar-literal
          (tptp-ebnf-v1:text-cons 45
            (tptp-ebnf-v1:text-cons 45
              (tptp-ebnf-v1:text-cons 62 (tptp-ebnf-v1:text-nil))))
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-grammar-literal-type-forall
      (head (metta-equation
        (tptp-ebnf-v1:project-grammar-literal
          (CstRuleV1 "syntax-bnf:grammar-literal-type-forall" ?start ?stop))
        (tptp-ebnf-v1:grammar-literal
          (tptp-ebnf-v1:text-cons 33
            (tptp-ebnf-v1:text-cons 62 (tptp-ebnf-v1:text-nil)))
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))

    (rule project-regex-expression
      (head (metta-equation
        (tptp-ebnf-v1:project-regex-expression
          (CstRuleV1 "syntax-bnf:regex-expression" ?start ?stop ?first ?tail))
        (tptp-ebnf-v1:regex-choice
          (tptp-ebnf-v1:regex-sequences-cons
            (tptp-ebnf-v1:project-regex-sequence ?first)
            (tptp-ebnf-v1:project-regex-alternatives ?tail))
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-regex-alternatives-empty
      (head (metta-equation
        (tptp-ebnf-v1:project-regex-alternatives
          (CstRuleV1 "syntax-bnf:regex-alternatives-empty" ?start ?stop))
        (tptp-ebnf-v1:regex-sequences-nil)))
      (body))
    (rule project-regex-alternatives-cons
      (head (metta-equation
        (tptp-ebnf-v1:project-regex-alternatives
          (CstRuleV1 "syntax-bnf:regex-alternatives-cons" ?start ?stop ?next ?tail))
        (tptp-ebnf-v1:regex-sequences-cons
          (tptp-ebnf-v1:project-regex-sequence ?next)
          (tptp-ebnf-v1:project-regex-alternatives ?tail))))
      (body))
    (rule project-regex-sequence-empty
      (head (metta-equation
        (tptp-ebnf-v1:project-regex-sequence
          (CstRuleV1 "syntax-bnf:regex-sequence-empty" ?start ?stop))
        (tptp-ebnf-v1:regex-sequence
          (tptp-ebnf-v1:regex-elements-nil)
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-regex-sequence-cons
      (head (metta-equation
        (tptp-ebnf-v1:project-regex-sequence
          (CstRuleV1 "syntax-bnf:regex-sequence-cons" ?start ?stop ?head ?tail))
        (tptp-ebnf-v1:regex-sequence
          (tptp-ebnf-v1:project-regex-elements
            (CstRuleV1 "syntax-bnf:regex-sequence-cons" ?start ?stop ?head ?tail))
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-regex-elements-empty
      (head (metta-equation
        (tptp-ebnf-v1:project-regex-elements
          (CstRuleV1 "syntax-bnf:regex-sequence-empty" ?start ?stop))
        (tptp-ebnf-v1:regex-elements-nil)))
      (body))
    (rule project-regex-elements-cons
      (head (metta-equation
        (tptp-ebnf-v1:project-regex-elements
          (CstRuleV1 "syntax-bnf:regex-sequence-cons" ?start ?stop ?head ?tail))
        (tptp-ebnf-v1:project-regex-element-onto
          ?head (tptp-ebnf-v1:project-regex-elements ?tail))))
      (body))
    (rule project-regex-element-piece
      (head (metta-equation
        (tptp-ebnf-v1:project-regex-element-onto
          (CstRuleV1 "syntax-bnf:regex-element-piece" ?start ?stop ?piece)
          ?tail)
        (tptp-ebnf-v1:regex-elements-cons
          (tptp-ebnf-v1:project-regex-piece ?piece) ?tail)))
      (body))
    (rule project-regex-element-layout
      (head (metta-equation
        (tptp-ebnf-v1:project-regex-element-onto
          (CstRuleV1 "syntax-bnf:regex-element-layout" ?start ?stop ?layout)
          ?tail)
        ?tail))
      (body))
    (rule project-regex-piece
      (head (metta-equation
        (tptp-ebnf-v1:project-regex-piece
          (CstRuleV1 "syntax-bnf:regex-piece" ?start ?stop ?atom ?repetition))
        (tptp-ebnf-v1:regex-piece
          (tptp-ebnf-v1:project-regex-atom ?atom)
          (tptp-ebnf-v1:project-regex-repetition ?repetition)
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-regex-repetition-once
      (head (metta-equation
        (tptp-ebnf-v1:project-regex-repetition
          (CstRuleV1 "syntax-bnf:regex-repetition-one" ?start ?stop))
        (tptp-ebnf-v1:regex-once)))
      (body))
    (rule project-regex-repetition-star
      (head (metta-equation
        (tptp-ebnf-v1:project-regex-repetition
          (CstRuleV1 "syntax-bnf:regex-repetition-zero-or-more" ?start ?stop))
        (tptp-ebnf-v1:regex-zero-or-more)))
      (body))
    (rule project-regex-atom-angle-reference
      (head (metta-equation
        (tptp-ebnf-v1:project-regex-atom
          (CstRuleV1 "syntax-bnf:regex-atom-angle-reference" ?start ?stop ?name))
        (tptp-ebnf-v1:regex-angle-reference
          (tptp-ebnf-v1:project-name ?name)
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-regex-atom-brace-reference
      (head (metta-equation
        (tptp-ebnf-v1:project-regex-atom
          (CstRuleV1 "syntax-bnf:regex-atom-brace-reference" ?start ?stop ?name))
        (tptp-ebnf-v1:regex-brace-reference
          (tptp-ebnf-v1:project-name ?name)
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-regex-atom-group
      (head (metta-equation
        (tptp-ebnf-v1:project-regex-atom
          (CstRuleV1 "syntax-bnf:regex-atom-group" ?start ?stop ?body))
        (tptp-ebnf-v1:regex-group
          (tptp-ebnf-v1:project-regex-expression ?body)
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-regex-atom-class
      (head (metta-equation
        (tptp-ebnf-v1:project-regex-atom
          (CstRuleV1 "syntax-bnf:regex-atom-class" ?start ?stop ?class))
        (tptp-ebnf-v1:project-regex-class ?class)))
      (body))
    (rule project-regex-atom-wildcard
      (head (metta-equation
        (tptp-ebnf-v1:project-regex-atom
          (CstRuleV1 "syntax-bnf:regex-atom-wildcard" ?start ?stop))
        (tptp-ebnf-v1:regex-wildcard
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))

    (rule project-regex-class
      (head (metta-equation
        (tptp-ebnf-v1:project-regex-class
          (CstRuleV1 "syntax-bnf:regex-class" ?start ?stop ?polarity ?body))
        (tptp-ebnf-v1:regex-class
          (tptp-ebnf-v1:project-regex-polarity ?polarity)
          (tptp-ebnf-v1:project-regex-class-items ?body)
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-regex-class-positive
      (head (metta-equation
        (tptp-ebnf-v1:project-regex-polarity
          (CstRuleV1 "syntax-bnf:regex-class-positive" ?start ?stop))
        (tptp-ebnf-v1:regex-class-positive)))
      (body))
    (rule project-regex-class-negated
      (head (metta-equation
        (tptp-ebnf-v1:project-regex-polarity
          (CstRuleV1 "syntax-bnf:regex-class-negated" ?start ?stop))
        (tptp-ebnf-v1:regex-class-negated)))
      (body))
    (rule project-regex-class-items-empty
      (head (metta-equation
        (tptp-ebnf-v1:project-regex-class-items
          (CstRuleV1 "syntax-bnf:regex-class-body-empty" ?start ?stop))
        (tptp-ebnf-v1:regex-class-items-nil)))
      (body))
    (rule project-regex-class-items-cons
      (head (metta-equation
        (tptp-ebnf-v1:project-regex-class-items
          (CstRuleV1 "syntax-bnf:regex-class-body-cons" ?start ?stop ?head ?tail))
        (tptp-ebnf-v1:regex-class-items-cons
          (tptp-ebnf-v1:project-regex-class-item ?head)
          (tptp-ebnf-v1:project-regex-class-items ?tail))))
      (body))
    (rule project-regex-class-items-final-hyphen
      (head (metta-equation
        (tptp-ebnf-v1:project-regex-class-items
          (CstRuleV1 "syntax-bnf:regex-class-body-final-hyphen" ?start ?stop))
        (tptp-ebnf-v1:regex-class-items-cons
          (tptp-ebnf-v1:regex-class-singleton
            (tptp-ebnf-v1:regex-endpoint-scalar
              (tptp-ebnf-v1:text-cons 45 (tptp-ebnf-v1:text-nil))
              (tptp-ebnf-v1:source-span ?start ?stop))
            (tptp-ebnf-v1:source-span ?start ?stop))
          (tptp-ebnf-v1:regex-class-items-nil))))
      (body))
    (rule project-regex-class-item-singleton
      (head (metta-equation
        (tptp-ebnf-v1:project-regex-class-item
          (CstRuleV1 "syntax-bnf:regex-class-item-singleton" ?start ?stop ?value))
        (tptp-ebnf-v1:regex-class-singleton
          (tptp-ebnf-v1:project-regex-endpoint ?value)
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-regex-class-item-range
      (head (metta-equation
        (tptp-ebnf-v1:project-regex-class-item
          (CstRuleV1 "syntax-bnf:regex-class-item-range" ?start ?stop ?first ?last))
        (tptp-ebnf-v1:regex-class-range
          (tptp-ebnf-v1:project-regex-endpoint ?first)
          (tptp-ebnf-v1:project-regex-endpoint ?last)
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-regex-endpoint-scalar
      (head (metta-equation
        (tptp-ebnf-v1:project-regex-endpoint
          (CstRuleV1 "syntax-bnf:regex-class-endpoint-scalar" ?start ?stop ?scalar))
        (tptp-ebnf-v1:regex-endpoint-scalar
          (tptp-ebnf-v1:text-cons
            (tptp-ebnf-v1:project-regex-class-scalar ?scalar)
            (tptp-ebnf-v1:text-nil))
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-regex-endpoint-escape
      (head (metta-equation
        (tptp-ebnf-v1:project-regex-endpoint
          (CstRuleV1 "syntax-bnf:regex-class-endpoint-escape" ?start ?stop ?escape))
        (tptp-ebnf-v1:project-regex-escape ?escape)))
      (body))
    (rule project-regex-escape-simple
      (head (metta-equation
        (tptp-ebnf-v1:project-regex-escape
          (CstRuleV1 "syntax-bnf:regex-class-escape-simple" ?start ?stop ?scalar))
        (tptp-ebnf-v1:regex-endpoint-simple-escape
          (tptp-ebnf-v1:text-cons
            (tptp-ebnf-v1:project-regex-simple-escape-scalar ?scalar)
            (tptp-ebnf-v1:text-nil))
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-regex-escape-octal-three
      (head (metta-equation
        (tptp-ebnf-v1:project-regex-escape
          (CstRuleV1 "syntax-bnf:regex-class-escape-octal-three" ?start ?stop
            ?hundreds ?tens ?ones))
        (tptp-ebnf-v1:regex-endpoint-octal-escape
          (tptp-ebnf-v1:text-cons
            (tptp-ebnf-v1:project-octal-leading-three ?hundreds)
            (tptp-ebnf-v1:text-cons
              (tptp-ebnf-v1:project-octal-digit ?tens)
              (tptp-ebnf-v1:text-cons
                (tptp-ebnf-v1:project-octal-digit ?ones)
                (tptp-ebnf-v1:text-nil))))
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-regex-escape-octal-two
      (head (metta-equation
        (tptp-ebnf-v1:project-regex-escape
          (CstRuleV1 "syntax-bnf:regex-class-escape-octal-two" ?start ?stop
            ?tens ?ones))
        (tptp-ebnf-v1:regex-endpoint-octal-escape
          (tptp-ebnf-v1:text-cons
            (tptp-ebnf-v1:project-octal-leading-two ?tens)
            (tptp-ebnf-v1:text-cons
              (tptp-ebnf-v1:project-octal-digit ?ones)
              (tptp-ebnf-v1:text-nil)))
          (tptp-ebnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-regex-class-scalar
      (head (metta-equation
        (tptp-ebnf-v1:project-regex-class-scalar
          (CstRuleV1 "syntax-bnf:lex-regex-class-scalar" ?start ?stop (cp ?scalar)))
        ?scalar))
      (body))
    (rule project-regex-simple-escape-scalar
      (head (metta-equation
        (tptp-ebnf-v1:project-regex-simple-escape-scalar
          (CstRuleV1 "syntax-bnf:lex-regex-simple-escape-scalar" ?start ?stop (cp ?scalar)))
        ?scalar))
      (body))
    (rule project-octal-leading-three
      (head (metta-equation
        (tptp-ebnf-v1:project-octal-leading-three
          (CstRuleV1 "syntax-bnf:lex-octal-leading-three" ?start ?stop (cp ?scalar)))
        ?scalar))
      (body))
    (rule project-octal-leading-two
      (head (metta-equation
        (tptp-ebnf-v1:project-octal-leading-two
          (CstRuleV1 "syntax-bnf:lex-octal-leading-two" ?start ?stop (cp ?scalar)))
        ?scalar))
      (body))
    (rule project-octal-digit
      (head (metta-equation
        (tptp-ebnf-v1:project-octal-digit
          (CstRuleV1 "syntax-bnf:lex-octal-digit" ?start ?stop (cp ?scalar)))
        ?scalar))
      (body))))
