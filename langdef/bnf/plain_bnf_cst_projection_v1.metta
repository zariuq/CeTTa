(gslt-presentation-v1 PlainBnfCstProjectionV1
  (signature
    (operator metta-equation 2)
    (operator CstRuleV1 3)
    (operator CstRuleV1 4)
    (operator CstRuleV1 5)
    (operator CstRuleV1 6)
    (operator CstRuleV1 7)
    (operator CstRuleV1 8)
    (operator CstRuleV1 9)
    (operator cp 1)

    (operator bnf-v1:project 1)
    (operator bnf-v1:project-lines 1)
    (operator bnf-v1:project-line-onto 2)
    (operator bnf-v1:project-rule 1)
    (operator bnf-v1:project-name 1)
    (operator bnf-v1:project-name-tail 1)
    (operator bnf-v1:project-name-char 1)
    (operator bnf-v1:project-comment-chars 1)
    (operator bnf-v1:project-line-scalar 1)
    (operator bnf-v1:project-expression 1)
    (operator bnf-v1:project-alternative-tail 1)
    (operator bnf-v1:project-sequence 1)
    (operator bnf-v1:project-sequence-tail 1)
    (operator bnf-v1:project-element 1)
    (operator bnf-v1:project-reference 1)
    (operator bnf-v1:project-literal 1)
    (operator bnf-v1:project-literal-chars 1)
    (operator bnf-v1:project-literal-char 1)
    (operator bnf-v1:project-literal-scalar 1)
    (operator bnf-v1:project-escape 1)

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
    (operator bnf-v1:literal 2))
  (equations)
  (rewrites
    (rule project-document
      (head (metta-equation
        (bnf-v1:project
          (CstRuleV1 "bnf-source-v1:document" ?start ?stop ?lines))
        (bnf-v1:document
          (bnf-v1:project-lines ?lines)
          (bnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-lines-empty
      (head (metta-equation
        (bnf-v1:project-lines
          (CstRuleV1 "bnf-source-v1:lines-empty" ?start ?stop))
        (bnf-v1:entries-nil)))
      (body))
    (rule project-lines-cons
      (head (metta-equation
        (bnf-v1:project-lines
          (CstRuleV1 "bnf-source-v1:lines-cons" ?start ?stop ?head ?tail))
        (bnf-v1:project-line-onto
          ?head (bnf-v1:project-lines ?tail))))
      (body))
    (rule project-line-rule
      (head (metta-equation
        (bnf-v1:project-line-onto
          (CstRuleV1 "bnf-source-v1:line-rule" ?start ?stop ?rule) ?tail)
        (bnf-v1:entries-cons (bnf-v1:project-rule ?rule) ?tail)))
      (body))
    (rule project-line-comment
      (head (metta-equation
        (bnf-v1:project-line-onto
          (CstRuleV1 "bnf-source-v1:line-comment" ?start ?stop
            (CstRuleV1 "bnf-source-v1:comment" ?comment-start ?comment-stop
              ?body ?end))
          ?tail)
        (bnf-v1:entries-cons
          (bnf-v1:comment
            (bnf-v1:project-comment-chars ?body)
            (bnf-v1:source-span ?comment-start ?comment-stop))
          ?tail)))
      (body))
    (rule project-line-blank
      (head (metta-equation
        (bnf-v1:project-line-onto
          (CstRuleV1 "bnf-source-v1:line-blank" ?start ?stop
            (CstRuleV1 "bnf-source-v1:blank" ?blank-start ?blank-stop
              ?spaces ?end))
          ?tail)
        (bnf-v1:entries-cons
          (bnf-v1:blank (bnf-v1:source-span ?blank-start ?blank-stop))
          ?tail)))
      (body))
    (rule project-rule
      (head (metta-equation
        (bnf-v1:project-rule
          (CstRuleV1 "bnf-source-v1:rule" ?start ?stop
            ?name ?before-assign ?after-assign ?expression ?end))
        (bnf-v1:rule
          (bnf-v1:project-name ?name)
          (bnf-v1:project-expression ?expression)
          (bnf-v1:source-span ?start ?stop))))
      (body))

    (rule project-name
      (head (metta-equation
        (bnf-v1:project-name
          (CstRuleV1 "bnf-source-v1:name" ?start ?stop ?head ?tail))
        (bnf-v1:text-cons
          (bnf-v1:project-name-char ?head)
          (bnf-v1:project-name-tail ?tail))))
      (body))
    (rule project-name-tail-empty
      (head (metta-equation
        (bnf-v1:project-name-tail
          (CstRuleV1 "bnf-source-v1:name-tail-empty" ?start ?stop))
        (bnf-v1:text-nil)))
      (body))
    (rule project-name-tail-cons
      (head (metta-equation
        (bnf-v1:project-name-tail
          (CstRuleV1 "bnf-source-v1:name-tail-cons" ?start ?stop ?head ?tail))
        (bnf-v1:text-cons
          (bnf-v1:project-name-char ?head)
          (bnf-v1:project-name-tail ?tail))))
      (body))
    (rule project-name-char
      (head (metta-equation
        (bnf-v1:project-name-char
          (CstRuleV1 "bnf-source-v1:lex-name-char" ?start ?stop (cp ?scalar)))
        ?scalar))
      (body))

    (rule project-comment-chars-empty
      (head (metta-equation
        (bnf-v1:project-comment-chars
          (CstRuleV1 "bnf-source-v1:comment-chars-empty" ?start ?stop))
        (bnf-v1:text-nil)))
      (body))
    (rule project-comment-chars-cons
      (head (metta-equation
        (bnf-v1:project-comment-chars
          (CstRuleV1 "bnf-source-v1:comment-chars-cons" ?start ?stop ?head ?tail))
        (bnf-v1:text-cons
          (bnf-v1:project-line-scalar ?head)
          (bnf-v1:project-comment-chars ?tail))))
      (body))
    (rule project-line-scalar
      (head (metta-equation
        (bnf-v1:project-line-scalar
          (CstRuleV1 "bnf-source-v1:lex-line-scalar" ?start ?stop (cp ?scalar)))
        ?scalar))
      (body))

    (rule project-expression
      (head (metta-equation
        (bnf-v1:project-expression
          (CstRuleV1 "bnf-source-v1:expression" ?start ?stop ?first ?tail))
        (bnf-v1:expression
          (bnf-v1:alternatives-cons
            (bnf-v1:project-sequence ?first)
            (bnf-v1:project-alternative-tail ?tail))
          (bnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-alternative-tail-empty
      (head (metta-equation
        (bnf-v1:project-alternative-tail
          (CstRuleV1 "bnf-source-v1:alternatives-empty" ?start ?stop))
        (bnf-v1:alternatives-nil)))
      (body))
    (rule project-alternative-tail-cons
      (head (metta-equation
        (bnf-v1:project-alternative-tail
          (CstRuleV1 "bnf-source-v1:alternatives-cons" ?start ?stop
            ?after-bar ?next ?tail))
        (bnf-v1:alternatives-cons
          (bnf-v1:project-sequence ?next)
          (bnf-v1:project-alternative-tail ?tail))))
      (body))
    (rule project-sequence-empty
      (head (metta-equation
        (bnf-v1:project-sequence
          (CstRuleV1 "bnf-source-v1:sequence-empty" ?start ?stop))
        (bnf-v1:alternative
          (bnf-v1:elements-nil)
          (bnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-sequence-cons
      (head (metta-equation
        (bnf-v1:project-sequence
          (CstRuleV1 "bnf-source-v1:sequence-cons" ?start ?stop ?head ?tail))
        (bnf-v1:alternative
          (bnf-v1:elements-cons
            (bnf-v1:project-element ?head)
            (bnf-v1:project-sequence-tail ?tail))
          (bnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-sequence-tail-empty
      (head (metta-equation
        (bnf-v1:project-sequence-tail
          (CstRuleV1 "bnf-source-v1:sequence-tail-empty" ?start ?stop))
        (bnf-v1:elements-nil)))
      (body))
    (rule project-sequence-tail-cons
      (head (metta-equation
        (bnf-v1:project-sequence-tail
          (CstRuleV1 "bnf-source-v1:sequence-tail-cons" ?start ?stop
            ?separator ?head ?tail))
        (bnf-v1:elements-cons
          (bnf-v1:project-element ?head)
          (bnf-v1:project-sequence-tail ?tail))))
      (body))
    (rule project-element-reference
      (head (metta-equation
        (bnf-v1:project-element
          (CstRuleV1 "bnf-source-v1:element-reference" ?start ?stop ?reference))
        (bnf-v1:project-reference ?reference)))
      (body))
    (rule project-element-literal
      (head (metta-equation
        (bnf-v1:project-element
          (CstRuleV1 "bnf-source-v1:element-literal" ?start ?stop ?literal))
        (bnf-v1:project-literal ?literal)))
      (body))
    (rule project-reference
      (head (metta-equation
        (bnf-v1:project-reference
          (CstRuleV1 "bnf-source-v1:reference" ?start ?stop ?name))
        (bnf-v1:reference
          (bnf-v1:project-name ?name)
          (bnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-literal
      (head (metta-equation
        (bnf-v1:project-literal
          (CstRuleV1 "bnf-source-v1:literal" ?start ?stop ?body))
        (bnf-v1:literal
          (bnf-v1:project-literal-chars ?body)
          (bnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-literal-chars-empty
      (head (metta-equation
        (bnf-v1:project-literal-chars
          (CstRuleV1 "bnf-source-v1:literal-chars-empty" ?start ?stop))
        (bnf-v1:text-nil)))
      (body))
    (rule project-literal-chars-cons
      (head (metta-equation
        (bnf-v1:project-literal-chars
          (CstRuleV1 "bnf-source-v1:literal-chars-cons" ?start ?stop ?head ?tail))
        (bnf-v1:text-cons
          (bnf-v1:project-literal-char ?head)
          (bnf-v1:project-literal-chars ?tail))))
      (body))
    (rule project-literal-char-scalar
      (head (metta-equation
        (bnf-v1:project-literal-char
          (CstRuleV1 "bnf-source-v1:literal-char-scalar" ?start ?stop ?scalar))
        (bnf-v1:project-literal-scalar ?scalar)))
      (body))
    (rule project-literal-char-escape
      (head (metta-equation
        (bnf-v1:project-literal-char
          (CstRuleV1 "bnf-source-v1:literal-char-escape" ?start ?stop ?escape))
        (bnf-v1:project-escape ?escape)))
      (body))
    (rule project-literal-scalar
      (head (metta-equation
        (bnf-v1:project-literal-scalar
          (CstRuleV1 "bnf-source-v1:lex-literal-scalar" ?start ?stop (cp ?scalar)))
        ?scalar))
      (body))
    (rule project-escape-quote
      (head (metta-equation
        (bnf-v1:project-escape
          (CstRuleV1 "bnf-source-v1:escape-quote" ?start ?stop))
        34))
      (body))
    (rule project-escape-backslash
      (head (metta-equation
        (bnf-v1:project-escape
          (CstRuleV1 "bnf-source-v1:escape-backslash" ?start ?stop))
        92))
      (body))
    (rule project-escape-newline
      (head (metta-equation
        (bnf-v1:project-escape
          (CstRuleV1 "bnf-source-v1:escape-newline" ?start ?stop))
        10))
      (body))
    (rule project-escape-return
      (head (metta-equation
        (bnf-v1:project-escape
          (CstRuleV1 "bnf-source-v1:escape-return" ?start ?stop))
        13))
      (body))
    (rule project-escape-tab
      (head (metta-equation
        (bnf-v1:project-escape
          (CstRuleV1 "bnf-source-v1:escape-tab" ?start ?stop))
        9))
      (body))))
