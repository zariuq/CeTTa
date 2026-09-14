; Projection of the CST computed from ebnf_source_v1.bnf. Labels identify
; ordered production occurrences emitted by plain-BNF denotation. No RHS is
; retained as an opaque string: nested operators become recursive grammar data.

(gslt-presentation-v1 EbnfCstProjectionV1
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

    (operator ebnf-v1:project 1)
    (operator ebnf-v1:project-lines 1)
    (operator ebnf-v1:project-line-onto 2)
    (operator ebnf-v1:project-rule 1)
    (operator ebnf-v1:project-name 1)
    (operator ebnf-v1:project-name-tail 1)
    (operator ebnf-v1:project-name-char 1)
    (operator ebnf-v1:project-comment-chars 1)
    (operator ebnf-v1:project-line-scalar 1)
    (operator ebnf-v1:project-expression 1)
    (operator ebnf-v1:project-alternative-tail 1)
    (operator ebnf-v1:project-sequence 1)
    (operator ebnf-v1:project-sequence-tail 1)
    (operator ebnf-v1:project-element 1)
    (operator ebnf-v1:project-atom 1)
    (operator ebnf-v1:apply-postfix 3)
    (operator ebnf-v1:group 2)
    (operator ebnf-v1:optional 2)
    (operator ebnf-v1:zero-or-more 2)
    (operator ebnf-v1:one-or-more 2)
    (operator ebnf-v1:project-reference 1)
    (operator ebnf-v1:project-literal 1)
    (operator ebnf-v1:project-literal-chars 1)
    (operator ebnf-v1:project-literal-char 1)
    (operator ebnf-v1:project-literal-scalar 1)
    (operator ebnf-v1:project-escape 1)

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
    (rule project-postfix-element
      (head (metta-equation
        (ebnf-v1:project-element
          (CstRuleV1 "element#" ?start ?stop ?atom ?postfix))
        (ebnf-v1:apply-postfix
          (ebnf-v1:project-atom ?atom) ?postfix
          (bnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-group
      (head (metta-equation
        (ebnf-v1:project-atom
          (CstRuleV1 "atom#xx" ?start ?stop ?body))
        (ebnf-v1:group
          (ebnf-v1:project-expression ?body)
          (bnf-v1:source-span ?start ?stop))))
      (body))
    (rule postfix-once
      (head (metta-equation
        (ebnf-v1:apply-postfix ?body (CstRuleV1 "postfix#" ?start ?stop) ?span)
        ?body))
      (body))
    (rule postfix-optional
      (head (metta-equation
        (ebnf-v1:apply-postfix ?body (CstRuleV1 "postfix#x" ?start ?stop) ?span)
        (ebnf-v1:optional ?body ?span)))
      (body))
    (rule postfix-star
      (head (metta-equation
        (ebnf-v1:apply-postfix ?body (CstRuleV1 "postfix#xx" ?start ?stop) ?span)
        (ebnf-v1:zero-or-more ?body ?span)))
      (body))
    (rule postfix-plus
      (head (metta-equation
        (ebnf-v1:apply-postfix ?body (CstRuleV1 "postfix#xxx" ?start ?stop) ?span)
        (ebnf-v1:one-or-more ?body ?span)))
      (body))
    (rule project-document
      (head (metta-equation
        (ebnf-v1:project
          (CstRuleV1 "document#" ?start ?stop ?lines))
        (bnf-v1:document
          (ebnf-v1:project-lines ?lines)
          (bnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-lines-empty
      (head (metta-equation
        (ebnf-v1:project-lines
          (CstRuleV1 "lines#" ?start ?stop))
        (bnf-v1:entries-nil)))
      (body))
    (rule project-lines-cons
      (head (metta-equation
        (ebnf-v1:project-lines
          (CstRuleV1 "lines#x" ?start ?stop ?head ?tail))
        (ebnf-v1:project-line-onto
          ?head (ebnf-v1:project-lines ?tail))))
      (body))
    (rule project-line-rule
      (head (metta-equation
        (ebnf-v1:project-line-onto
          (CstRuleV1 "line#" ?start ?stop ?rule) ?tail)
        (bnf-v1:entries-cons (ebnf-v1:project-rule ?rule) ?tail)))
      (body))
    (rule project-line-comment
      (head (metta-equation
        (ebnf-v1:project-line-onto
          (CstRuleV1 "line#x" ?start ?stop
            (CstRuleV1 "comment#" ?comment-start ?comment-stop
              ?body ?end))
          ?tail)
        (bnf-v1:entries-cons
          (bnf-v1:comment
            (ebnf-v1:project-comment-chars ?body)
            (bnf-v1:source-span ?comment-start ?comment-stop))
          ?tail)))
      (body))
    (rule project-line-blank
      (head (metta-equation
        (ebnf-v1:project-line-onto
          (CstRuleV1 "line#xx" ?start ?stop
            (CstRuleV1 "blank#" ?blank-start ?blank-stop
              ?spaces ?end))
          ?tail)
        (bnf-v1:entries-cons
          (bnf-v1:blank (bnf-v1:source-span ?blank-start ?blank-stop))
          ?tail)))
      (body))
    (rule project-rule
      (head (metta-equation
        (ebnf-v1:project-rule
          (CstRuleV1 "rule#" ?start ?stop
            ?name ?before-assign ?after-assign ?expression ?end))
        (bnf-v1:rule
          (ebnf-v1:project-name ?name)
          (ebnf-v1:project-expression ?expression)
          (bnf-v1:source-span ?start ?stop))))
      (body))

    (rule project-name
      (head (metta-equation
        (ebnf-v1:project-name
          (CstRuleV1 "name#" ?start ?stop ?head ?tail))
        (bnf-v1:text-cons
          (ebnf-v1:project-name-char ?head)
          (ebnf-v1:project-name-tail ?tail))))
      (body))
    (rule project-name-tail-empty
      (head (metta-equation
        (ebnf-v1:project-name-tail
          (CstRuleV1 "name-tail#" ?start ?stop))
        (bnf-v1:text-nil)))
      (body))
    (rule project-name-tail-cons
      (head (metta-equation
        (ebnf-v1:project-name-tail
          (CstRuleV1 "name-tail#x" ?start ?stop ?head ?tail))
        (bnf-v1:text-cons
          (ebnf-v1:project-name-char ?head)
          (ebnf-v1:project-name-tail ?tail))))
      (body))
    (rule project-name-char
      (head (metta-equation
        (ebnf-v1:project-name-char
          (CstRuleV1 "bnf-source-v1:lex-name-char" ?start ?stop (cp ?scalar)))
        ?scalar))
      (body))

    (rule project-comment-chars-empty
      (head (metta-equation
        (ebnf-v1:project-comment-chars
          (CstRuleV1 "comment-chars#" ?start ?stop))
        (bnf-v1:text-nil)))
      (body))
    (rule project-comment-chars-cons
      (head (metta-equation
        (ebnf-v1:project-comment-chars
          (CstRuleV1 "comment-chars#x" ?start ?stop ?head ?tail))
        (bnf-v1:text-cons
          (ebnf-v1:project-line-scalar ?head)
          (ebnf-v1:project-comment-chars ?tail))))
      (body))
    (rule project-line-scalar
      (head (metta-equation
        (ebnf-v1:project-line-scalar
          (CstRuleV1 "bnf-source-v1:lex-line-scalar" ?start ?stop (cp ?scalar)))
        ?scalar))
      (body))

    (rule project-expression
      (head (metta-equation
        (ebnf-v1:project-expression
          (CstRuleV1 "expression#" ?start ?stop ?first ?tail))
        (bnf-v1:expression
          (bnf-v1:alternatives-cons
            (ebnf-v1:project-sequence ?first)
            (ebnf-v1:project-alternative-tail ?tail))
          (bnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-alternative-tail-empty
      (head (metta-equation
        (ebnf-v1:project-alternative-tail
          (CstRuleV1 "alternative-tail#" ?start ?stop))
        (bnf-v1:alternatives-nil)))
      (body))
    (rule project-alternative-tail-cons
      (head (metta-equation
        (ebnf-v1:project-alternative-tail
          (CstRuleV1 "alternative-tail#x" ?start ?stop
            ?after-bar ?next ?tail))
        (bnf-v1:alternatives-cons
          (ebnf-v1:project-sequence ?next)
          (ebnf-v1:project-alternative-tail ?tail))))
      (body))
    (rule project-sequence-empty
      (head (metta-equation
        (ebnf-v1:project-sequence
          (CstRuleV1 "sequence#" ?start ?stop))
        (bnf-v1:alternative
          (bnf-v1:elements-nil)
          (bnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-sequence-cons
      (head (metta-equation
        (ebnf-v1:project-sequence
          (CstRuleV1 "sequence#x" ?start ?stop ?head ?tail))
        (bnf-v1:alternative
          (bnf-v1:elements-cons
            (ebnf-v1:project-element ?head)
            (ebnf-v1:project-sequence-tail ?tail))
          (bnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-sequence-tail-empty
      (head (metta-equation
        (ebnf-v1:project-sequence-tail
          (CstRuleV1 "sequence-tail#" ?start ?stop))
        (bnf-v1:elements-nil)))
      (body))
    (rule project-sequence-tail-cons
      (head (metta-equation
        (ebnf-v1:project-sequence-tail
          (CstRuleV1 "sequence-tail#x" ?start ?stop
            ?separator ?head ?tail))
        (bnf-v1:elements-cons
          (ebnf-v1:project-element ?head)
          (ebnf-v1:project-sequence-tail ?tail))))
      (body))
    (rule project-element-reference
      (head (metta-equation
        (ebnf-v1:project-atom
          (CstRuleV1 "atom#" ?start ?stop ?reference))
        (ebnf-v1:project-reference ?reference)))
      (body))
    (rule project-element-literal
      (head (metta-equation
        (ebnf-v1:project-atom
          (CstRuleV1 "atom#x" ?start ?stop ?literal))
        (ebnf-v1:project-literal ?literal)))
      (body))
    (rule project-reference
      (head (metta-equation
        (ebnf-v1:project-reference
          (CstRuleV1 "reference#" ?start ?stop ?name))
        (bnf-v1:reference
          (ebnf-v1:project-name ?name)
          (bnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-literal
      (head (metta-equation
        (ebnf-v1:project-literal
          (CstRuleV1 "literal#" ?start ?stop ?body))
        (bnf-v1:literal
          (ebnf-v1:project-literal-chars ?body)
          (bnf-v1:source-span ?start ?stop))))
      (body))
    (rule project-literal-chars-empty
      (head (metta-equation
        (ebnf-v1:project-literal-chars
          (CstRuleV1 "literal-chars#" ?start ?stop))
        (bnf-v1:text-nil)))
      (body))
    (rule project-literal-chars-cons
      (head (metta-equation
        (ebnf-v1:project-literal-chars
          (CstRuleV1 "literal-chars#x" ?start ?stop ?head ?tail))
        (bnf-v1:text-cons
          (ebnf-v1:project-literal-char ?head)
          (ebnf-v1:project-literal-chars ?tail))))
      (body))
    (rule project-literal-char-scalar
      (head (metta-equation
        (ebnf-v1:project-literal-char
          (CstRuleV1 "literal-char#" ?start ?stop ?scalar))
        (ebnf-v1:project-literal-scalar ?scalar)))
      (body))
    (rule project-literal-char-escape
      (head (metta-equation
        (ebnf-v1:project-literal-char
          (CstRuleV1 "literal-char#x" ?start ?stop ?escape))
        (ebnf-v1:project-escape ?escape)))
      (body))
    (rule project-literal-scalar
      (head (metta-equation
        (ebnf-v1:project-literal-scalar
          (CstRuleV1 "bnf-source-v1:lex-literal-scalar" ?start ?stop (cp ?scalar)))
        ?scalar))
      (body))
    (rule project-escape-quote
      (head (metta-equation
        (ebnf-v1:project-escape
          (CstRuleV1 "escape#" ?start ?stop))
        34))
      (body))
    (rule project-escape-backslash
      (head (metta-equation
        (ebnf-v1:project-escape
          (CstRuleV1 "escape#x" ?start ?stop))
        92))
      (body))
    (rule project-escape-newline
      (head (metta-equation
        (ebnf-v1:project-escape
          (CstRuleV1 "escape#xx" ?start ?stop))
        10))
      (body))
    (rule project-escape-return
      (head (metta-equation
        (ebnf-v1:project-escape
          (CstRuleV1 "escape#xxx" ?start ?stop))
        13))
      (body))
    (rule project-escape-tab
      (head (metta-equation
        (ebnf-v1:project-escape
          (CstRuleV1 "escape#xxxx" ?start ?stop))
        9))
      (body))))
