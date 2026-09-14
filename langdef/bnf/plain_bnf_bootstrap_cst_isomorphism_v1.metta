; Explicit correspondence between the rule-occurrence labels denoted from the
; plain-BNF self-description and the bootstrap parser's administrative labels.
; The tree maps preserve every source span and child occurrence.

(gslt-presentation-v1 PlainBnfBootstrapCstIsomorphismV1
  (signature
    (operator metta-equation 2)
    (operator CstRuleV1 3)
    (operator CstRuleV1 4)
    (operator CstRuleV1 5)
    (operator CstRuleV1 6)
    (operator CstRuleV1 7)
    (operator CstRuleV1 8)
    (operator CstRuleV1 9)
    (operator CstRuleV1 10)
    (operator CstRuleV1 11)
    (operator cp 1)
    (operator bnf-v1:generated-cst->bootstrap-cst 1)
    (operator bnf-v1:bootstrap-cst->generated-cst 1)
    (operator bnf-v1:generated-label->bootstrap-label 1)
    (operator bnf-v1:bootstrap-label->generated-label 1))
  (equations)
  (rewrites
    (rule generated-cp
      (head (metta-equation
        (bnf-v1:generated-cst->bootstrap-cst (cp ?scalar))
        (cp ?scalar)))
      (body))
    (rule generated-rule-0
      (head (metta-equation
        (bnf-v1:generated-cst->bootstrap-cst
          (CstRuleV1 ?label ?start ?stop))
        (CstRuleV1
          (bnf-v1:generated-label->bootstrap-label ?label)
          ?start ?stop)))
      (body))
    (rule generated-rule-1
      (head (metta-equation
        (bnf-v1:generated-cst->bootstrap-cst
          (CstRuleV1 ?label ?start ?stop ?a))
        (CstRuleV1
          (bnf-v1:generated-label->bootstrap-label ?label)
          ?start ?stop
          (bnf-v1:generated-cst->bootstrap-cst ?a))))
      (body))
    (rule generated-rule-2
      (head (metta-equation
        (bnf-v1:generated-cst->bootstrap-cst
          (CstRuleV1 ?label ?start ?stop ?a ?b))
        (CstRuleV1
          (bnf-v1:generated-label->bootstrap-label ?label)
          ?start ?stop
          (bnf-v1:generated-cst->bootstrap-cst ?a)
          (bnf-v1:generated-cst->bootstrap-cst ?b))))
      (body))
    (rule generated-rule-3
      (head (metta-equation
        (bnf-v1:generated-cst->bootstrap-cst
          (CstRuleV1 ?label ?start ?stop ?a ?b ?c))
        (CstRuleV1
          (bnf-v1:generated-label->bootstrap-label ?label)
          ?start ?stop
          (bnf-v1:generated-cst->bootstrap-cst ?a)
          (bnf-v1:generated-cst->bootstrap-cst ?b)
          (bnf-v1:generated-cst->bootstrap-cst ?c))))
      (body))
    (rule generated-rule-4
      (head (metta-equation
        (bnf-v1:generated-cst->bootstrap-cst
          (CstRuleV1 ?label ?start ?stop ?a ?b ?c ?d))
        (CstRuleV1
          (bnf-v1:generated-label->bootstrap-label ?label)
          ?start ?stop
          (bnf-v1:generated-cst->bootstrap-cst ?a)
          (bnf-v1:generated-cst->bootstrap-cst ?b)
          (bnf-v1:generated-cst->bootstrap-cst ?c)
          (bnf-v1:generated-cst->bootstrap-cst ?d))))
      (body))
    (rule generated-rule-5
      (head (metta-equation
        (bnf-v1:generated-cst->bootstrap-cst
          (CstRuleV1 ?label ?start ?stop ?a ?b ?c ?d ?e))
        (CstRuleV1
          (bnf-v1:generated-label->bootstrap-label ?label)
          ?start ?stop
          (bnf-v1:generated-cst->bootstrap-cst ?a)
          (bnf-v1:generated-cst->bootstrap-cst ?b)
          (bnf-v1:generated-cst->bootstrap-cst ?c)
          (bnf-v1:generated-cst->bootstrap-cst ?d)
          (bnf-v1:generated-cst->bootstrap-cst ?e))))
      (body))
    (rule generated-rule-6
      (head (metta-equation
        (bnf-v1:generated-cst->bootstrap-cst
          (CstRuleV1 ?label ?start ?stop ?a ?b ?c ?d ?e ?f))
        (CstRuleV1
          (bnf-v1:generated-label->bootstrap-label ?label)
          ?start ?stop
          (bnf-v1:generated-cst->bootstrap-cst ?a)
          (bnf-v1:generated-cst->bootstrap-cst ?b)
          (bnf-v1:generated-cst->bootstrap-cst ?c)
          (bnf-v1:generated-cst->bootstrap-cst ?d)
          (bnf-v1:generated-cst->bootstrap-cst ?e)
          (bnf-v1:generated-cst->bootstrap-cst ?f))))
      (body))
    (rule generated-rule-7
      (head (metta-equation
        (bnf-v1:generated-cst->bootstrap-cst
          (CstRuleV1 ?label ?start ?stop ?a ?b ?c ?d ?e ?f ?g))
        (CstRuleV1
          (bnf-v1:generated-label->bootstrap-label ?label)
          ?start ?stop
          (bnf-v1:generated-cst->bootstrap-cst ?a)
          (bnf-v1:generated-cst->bootstrap-cst ?b)
          (bnf-v1:generated-cst->bootstrap-cst ?c)
          (bnf-v1:generated-cst->bootstrap-cst ?d)
          (bnf-v1:generated-cst->bootstrap-cst ?e)
          (bnf-v1:generated-cst->bootstrap-cst ?f)
          (bnf-v1:generated-cst->bootstrap-cst ?g))))
      (body))
    (rule generated-rule-8
      (head (metta-equation
        (bnf-v1:generated-cst->bootstrap-cst
          (CstRuleV1 ?label ?start ?stop ?a ?b ?c ?d ?e ?f ?g ?h))
        (CstRuleV1
          (bnf-v1:generated-label->bootstrap-label ?label)
          ?start ?stop
          (bnf-v1:generated-cst->bootstrap-cst ?a)
          (bnf-v1:generated-cst->bootstrap-cst ?b)
          (bnf-v1:generated-cst->bootstrap-cst ?c)
          (bnf-v1:generated-cst->bootstrap-cst ?d)
          (bnf-v1:generated-cst->bootstrap-cst ?e)
          (bnf-v1:generated-cst->bootstrap-cst ?f)
          (bnf-v1:generated-cst->bootstrap-cst ?g)
          (bnf-v1:generated-cst->bootstrap-cst ?h))))
      (body))

    (rule bootstrap-cp
      (head (metta-equation
        (bnf-v1:bootstrap-cst->generated-cst (cp ?scalar))
        (cp ?scalar)))
      (body))
    (rule bootstrap-rule-0
      (head (metta-equation
        (bnf-v1:bootstrap-cst->generated-cst
          (CstRuleV1 ?label ?start ?stop))
        (CstRuleV1
          (bnf-v1:bootstrap-label->generated-label ?label)
          ?start ?stop)))
      (body))
    (rule bootstrap-rule-1
      (head (metta-equation
        (bnf-v1:bootstrap-cst->generated-cst
          (CstRuleV1 ?label ?start ?stop ?a))
        (CstRuleV1
          (bnf-v1:bootstrap-label->generated-label ?label)
          ?start ?stop
          (bnf-v1:bootstrap-cst->generated-cst ?a))))
      (body))
    (rule bootstrap-rule-2
      (head (metta-equation
        (bnf-v1:bootstrap-cst->generated-cst
          (CstRuleV1 ?label ?start ?stop ?a ?b))
        (CstRuleV1
          (bnf-v1:bootstrap-label->generated-label ?label)
          ?start ?stop
          (bnf-v1:bootstrap-cst->generated-cst ?a)
          (bnf-v1:bootstrap-cst->generated-cst ?b))))
      (body))
    (rule bootstrap-rule-3
      (head (metta-equation
        (bnf-v1:bootstrap-cst->generated-cst
          (CstRuleV1 ?label ?start ?stop ?a ?b ?c))
        (CstRuleV1
          (bnf-v1:bootstrap-label->generated-label ?label)
          ?start ?stop
          (bnf-v1:bootstrap-cst->generated-cst ?a)
          (bnf-v1:bootstrap-cst->generated-cst ?b)
          (bnf-v1:bootstrap-cst->generated-cst ?c))))
      (body))
    (rule bootstrap-rule-4
      (head (metta-equation
        (bnf-v1:bootstrap-cst->generated-cst
          (CstRuleV1 ?label ?start ?stop ?a ?b ?c ?d))
        (CstRuleV1
          (bnf-v1:bootstrap-label->generated-label ?label)
          ?start ?stop
          (bnf-v1:bootstrap-cst->generated-cst ?a)
          (bnf-v1:bootstrap-cst->generated-cst ?b)
          (bnf-v1:bootstrap-cst->generated-cst ?c)
          (bnf-v1:bootstrap-cst->generated-cst ?d))))
      (body))
    (rule bootstrap-rule-5
      (head (metta-equation
        (bnf-v1:bootstrap-cst->generated-cst
          (CstRuleV1 ?label ?start ?stop ?a ?b ?c ?d ?e))
        (CstRuleV1
          (bnf-v1:bootstrap-label->generated-label ?label)
          ?start ?stop
          (bnf-v1:bootstrap-cst->generated-cst ?a)
          (bnf-v1:bootstrap-cst->generated-cst ?b)
          (bnf-v1:bootstrap-cst->generated-cst ?c)
          (bnf-v1:bootstrap-cst->generated-cst ?d)
          (bnf-v1:bootstrap-cst->generated-cst ?e))))
      (body))
    (rule bootstrap-rule-6
      (head (metta-equation
        (bnf-v1:bootstrap-cst->generated-cst
          (CstRuleV1 ?label ?start ?stop ?a ?b ?c ?d ?e ?f))
        (CstRuleV1
          (bnf-v1:bootstrap-label->generated-label ?label)
          ?start ?stop
          (bnf-v1:bootstrap-cst->generated-cst ?a)
          (bnf-v1:bootstrap-cst->generated-cst ?b)
          (bnf-v1:bootstrap-cst->generated-cst ?c)
          (bnf-v1:bootstrap-cst->generated-cst ?d)
          (bnf-v1:bootstrap-cst->generated-cst ?e)
          (bnf-v1:bootstrap-cst->generated-cst ?f))))
      (body))
    (rule bootstrap-rule-7
      (head (metta-equation
        (bnf-v1:bootstrap-cst->generated-cst
          (CstRuleV1 ?label ?start ?stop ?a ?b ?c ?d ?e ?f ?g))
        (CstRuleV1
          (bnf-v1:bootstrap-label->generated-label ?label)
          ?start ?stop
          (bnf-v1:bootstrap-cst->generated-cst ?a)
          (bnf-v1:bootstrap-cst->generated-cst ?b)
          (bnf-v1:bootstrap-cst->generated-cst ?c)
          (bnf-v1:bootstrap-cst->generated-cst ?d)
          (bnf-v1:bootstrap-cst->generated-cst ?e)
          (bnf-v1:bootstrap-cst->generated-cst ?f)
          (bnf-v1:bootstrap-cst->generated-cst ?g))))
      (body))
    (rule bootstrap-rule-8
      (head (metta-equation
        (bnf-v1:bootstrap-cst->generated-cst
          (CstRuleV1 ?label ?start ?stop ?a ?b ?c ?d ?e ?f ?g ?h))
        (CstRuleV1
          (bnf-v1:bootstrap-label->generated-label ?label)
          ?start ?stop
          (bnf-v1:bootstrap-cst->generated-cst ?a)
          (bnf-v1:bootstrap-cst->generated-cst ?b)
          (bnf-v1:bootstrap-cst->generated-cst ?c)
          (bnf-v1:bootstrap-cst->generated-cst ?d)
          (bnf-v1:bootstrap-cst->generated-cst ?e)
          (bnf-v1:bootstrap-cst->generated-cst ?f)
          (bnf-v1:bootstrap-cst->generated-cst ?g)
          (bnf-v1:bootstrap-cst->generated-cst ?h))))
      (body))

    (rule label-document
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "document#")
        "bnf-source-v1:document")) (body))
    (rule label-lines-empty
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "lines#")
        "bnf-source-v1:lines-empty")) (body))
    (rule label-lines-cons
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "lines#x")
        "bnf-source-v1:lines-cons")) (body))
    (rule label-line-rule
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "line#")
        "bnf-source-v1:line-rule")) (body))
    (rule label-line-comment
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "line#x")
        "bnf-source-v1:line-comment")) (body))
    (rule label-line-blank
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "line#xx")
        "bnf-source-v1:line-blank")) (body))
    (rule label-rule
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "rule#")
        "bnf-source-v1:rule")) (body))
    (rule label-comment
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "comment#")
        "bnf-source-v1:comment")) (body))
    (rule label-blank
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "blank#")
        "bnf-source-v1:blank")) (body))
    (rule label-name
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "name#")
        "bnf-source-v1:name")) (body))
    (rule label-name-tail-empty
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "name-tail#")
        "bnf-source-v1:name-tail-empty")) (body))
    (rule label-name-tail-cons
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "name-tail#x")
        "bnf-source-v1:name-tail-cons")) (body))
    (rule label-expression
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "expression#")
        "bnf-source-v1:expression")) (body))
    (rule label-alternatives-empty
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "alternative-tail#")
        "bnf-source-v1:alternatives-empty")) (body))
    (rule label-alternatives-cons
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "alternative-tail#x")
        "bnf-source-v1:alternatives-cons")) (body))
    (rule label-sequence-empty
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "sequence#")
        "bnf-source-v1:sequence-empty")) (body))
    (rule label-sequence-cons
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "sequence#x")
        "bnf-source-v1:sequence-cons")) (body))
    (rule label-sequence-tail-empty
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "sequence-tail#")
        "bnf-source-v1:sequence-tail-empty")) (body))
    (rule label-sequence-tail-cons
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "sequence-tail#x")
        "bnf-source-v1:sequence-tail-cons")) (body))
    (rule label-element-reference
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "element#")
        "bnf-source-v1:element-reference")) (body))
    (rule label-element-literal
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "element#x")
        "bnf-source-v1:element-literal")) (body))
    (rule label-reference
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "reference#")
        "bnf-source-v1:reference")) (body))
    (rule label-literal
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "literal#")
        "bnf-source-v1:literal")) (body))
    (rule label-literal-chars-empty
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "literal-chars#")
        "bnf-source-v1:literal-chars-empty")) (body))
    (rule label-literal-chars-cons
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "literal-chars#x")
        "bnf-source-v1:literal-chars-cons")) (body))
    (rule label-literal-char-scalar
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "literal-char#")
        "bnf-source-v1:literal-char-scalar")) (body))
    (rule label-literal-char-escape
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "literal-char#x")
        "bnf-source-v1:literal-char-escape")) (body))
    (rule label-escape-quote
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "escape#")
        "bnf-source-v1:escape-quote")) (body))
    (rule label-escape-backslash
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "escape#x")
        "bnf-source-v1:escape-backslash")) (body))
    (rule label-escape-newline
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "escape#xx")
        "bnf-source-v1:escape-newline")) (body))
    (rule label-escape-return
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "escape#xxx")
        "bnf-source-v1:escape-return")) (body))
    (rule label-escape-tab
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "escape#xxxx")
        "bnf-source-v1:escape-tab")) (body))
    (rule label-spaces-empty
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "spaces#")
        "bnf-source-v1:spaces-empty")) (body))
    (rule label-spaces-cons
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "spaces#x")
        "bnf-source-v1:spaces-cons")) (body))
    (rule label-spaces-one
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "spaces-one#")
        "bnf-source-v1:spaces-one")) (body))
    (rule label-comment-chars-empty
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "comment-chars#")
        "bnf-source-v1:comment-chars-empty")) (body))
    (rule label-comment-chars-cons
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "comment-chars#x")
        "bnf-source-v1:comment-chars-cons")) (body))
    (rule label-line-end-lf
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "line-end#")
        "bnf-source-v1:line-end-lf")) (body))
    (rule label-line-end-crlf
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label "line-end#x")
        "bnf-source-v1:line-end-crlf")) (body))
    (rule label-lex-name-char
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label
          "bnf-source-v1:lex-name-char")
        "bnf-source-v1:lex-name-char")) (body))
    (rule label-lex-literal-scalar
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label
          "bnf-source-v1:lex-literal-scalar")
        "bnf-source-v1:lex-literal-scalar")) (body))
    (rule label-lex-horizontal-space
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label
          "bnf-source-v1:lex-horizontal-space")
        "bnf-source-v1:lex-horizontal-space")) (body))
    (rule label-lex-line-scalar
      (head (metta-equation
        (bnf-v1:generated-label->bootstrap-label
          "bnf-source-v1:lex-line-scalar")
        "bnf-source-v1:lex-line-scalar")) (body))

    (rule inverse-document
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:document")
        "document#")) (body))
    (rule inverse-lines-empty
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:lines-empty")
        "lines#")) (body))
    (rule inverse-lines-cons
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:lines-cons")
        "lines#x")) (body))
    (rule inverse-line-rule
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:line-rule")
        "line#")) (body))
    (rule inverse-line-comment
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:line-comment")
        "line#x")) (body))
    (rule inverse-line-blank
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:line-blank")
        "line#xx")) (body))
    (rule inverse-rule
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:rule")
        "rule#")) (body))
    (rule inverse-comment
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:comment")
        "comment#")) (body))
    (rule inverse-blank
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:blank")
        "blank#")) (body))
    (rule inverse-name
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:name")
        "name#")) (body))
    (rule inverse-name-tail-empty
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:name-tail-empty")
        "name-tail#")) (body))
    (rule inverse-name-tail-cons
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:name-tail-cons")
        "name-tail#x")) (body))
    (rule inverse-expression
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:expression")
        "expression#")) (body))
    (rule inverse-alternatives-empty
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:alternatives-empty")
        "alternative-tail#")) (body))
    (rule inverse-alternatives-cons
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:alternatives-cons")
        "alternative-tail#x")) (body))
    (rule inverse-sequence-empty
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:sequence-empty")
        "sequence#")) (body))
    (rule inverse-sequence-cons
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:sequence-cons")
        "sequence#x")) (body))
    (rule inverse-sequence-tail-empty
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:sequence-tail-empty")
        "sequence-tail#")) (body))
    (rule inverse-sequence-tail-cons
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:sequence-tail-cons")
        "sequence-tail#x")) (body))
    (rule inverse-element-reference
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:element-reference")
        "element#")) (body))
    (rule inverse-element-literal
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:element-literal")
        "element#x")) (body))
    (rule inverse-reference
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:reference")
        "reference#")) (body))
    (rule inverse-literal
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:literal")
        "literal#")) (body))
    (rule inverse-literal-chars-empty
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:literal-chars-empty")
        "literal-chars#")) (body))
    (rule inverse-literal-chars-cons
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:literal-chars-cons")
        "literal-chars#x")) (body))
    (rule inverse-literal-char-scalar
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:literal-char-scalar")
        "literal-char#")) (body))
    (rule inverse-literal-char-escape
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:literal-char-escape")
        "literal-char#x")) (body))
    (rule inverse-escape-quote
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:escape-quote")
        "escape#")) (body))
    (rule inverse-escape-backslash
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:escape-backslash")
        "escape#x")) (body))
    (rule inverse-escape-newline
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:escape-newline")
        "escape#xx")) (body))
    (rule inverse-escape-return
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:escape-return")
        "escape#xxx")) (body))
    (rule inverse-escape-tab
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:escape-tab")
        "escape#xxxx")) (body))
    (rule inverse-spaces-empty
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:spaces-empty")
        "spaces#")) (body))
    (rule inverse-spaces-cons
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:spaces-cons")
        "spaces#x")) (body))
    (rule inverse-spaces-one
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:spaces-one")
        "spaces-one#")) (body))
    (rule inverse-comment-chars-empty
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:comment-chars-empty")
        "comment-chars#")) (body))
    (rule inverse-comment-chars-cons
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:comment-chars-cons")
        "comment-chars#x")) (body))
    (rule inverse-line-end-lf
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:line-end-lf")
        "line-end#")) (body))
    (rule inverse-line-end-crlf
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label "bnf-source-v1:line-end-crlf")
        "line-end#x")) (body))
    (rule inverse-lex-name-char
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label
          "bnf-source-v1:lex-name-char")
        "bnf-source-v1:lex-name-char")) (body))
    (rule inverse-lex-literal-scalar
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label
          "bnf-source-v1:lex-literal-scalar")
        "bnf-source-v1:lex-literal-scalar")) (body))
    (rule inverse-lex-horizontal-space
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label
          "bnf-source-v1:lex-horizontal-space")
        "bnf-source-v1:lex-horizontal-space")) (body))
    (rule inverse-lex-line-scalar
      (head (metta-equation
        (bnf-v1:bootstrap-label->generated-label
          "bnf-source-v1:lex-line-scalar")
        "bnf-source-v1:lex-line-scalar")) (body))))
