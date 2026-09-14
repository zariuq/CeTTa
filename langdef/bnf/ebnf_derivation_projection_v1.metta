; Projection of finite lowered CSTs to source-level EBNF derivations.
; Each accepted CST remains one derivation. Choices and zero-width literals
; remain explicit; generated nonterminal names are not returned as syntax.
; This transformation is not a byte-level parser or a proof-replay checker.
; Left-recursive helpers accumulate iterations in a private reversed form.
; A single traversal at each root restores public source order, including
; nested repetitions. No growing-list append or ambiguity deduplication runs.
(gslt-presentation-v1 EbnfDerivationProjectionV1
  (signature
    (operator metta-equation 2)
    (operator ebnf-v1:project-derivations 4)
    (operator bnf-v1:document 2)
    (operator bnf-v1:grammar-authority 2)
    (operator bnf-v1:start 1)
    (operator bnf-v1:lexical-environment 1)
    (operator ebnf-v1:project-roots 3)
    (operator ebnf-v1:projection-context 3)
    (operator langdef:expression->list 1)
    (operator EBNF:DerivationsNil 0)
    (operator LCons 2)
    (operator EBNF:DerivationsCons 2)
    (operator ebnf-v1:project-tree 3)
    (operator ebnf-v1:project-found 4)
    (operator ebnf-v1:find-definition 3)
    (operator bnf-v1:entries-nil 0)
    (operator ebnf-v1:find-lexical 2)
    (operator bnf-v1:entries-cons 2)
    (operator bnf-v1:rule 3)
    (operator ebnf-v1:definition-choice 6)
    (operator langdef:ground-equal 2)
    (operator ebnf-v1:found-rule 2)
    (operator bnf-v1:comment 2)
    (operator bnf-v1:blank 1)
    (operator bnf-v1:lexical-declarations-cons 2)
    (operator bnf-v1:lexical-declaration 5)
    (operator ebnf-v1:lexical-choice 5)
    (operator ebnf-v1:found-lexical 2)
    (operator bnf-v1:expression 2)
    (operator ebnf-v1:project-alternative 8)
    (operator ebnf-v1:helper 3)
    (operator ebnf-v1:select-alternative 5)
    (operator bnf-v1:suffix-start 0)
    (operator cp 1)
    (operator ebnf-v1:checked-lexical 8)
    (operator bnf-v1:text->string 1)
    (operator + 2)
    (operator EBNF:Lexical 5)
    (operator EBNF:InputSpan 2)
    (operator bnf-v1:alternatives-cons 2)
    (operator ebnf-v1:alternative-choice 7)
    (operator bnf-v1:text-append 2)
    (operator bnf-v1:alternative 2)
    (operator ebnf-v1:selected-alternative 3)
    (operator bnf-v1:suffix-next 1)
    (operator ebnf-v1:origins-nil 0)
    (operator ebnf-v1:authored 0)
    (operator ebnf-v1:origins-cons 2)
    (operator ebnf-v1:helper-origin 4)
    (operator ebnf-v1:helper-choice 6)
    (operator ebnf-v1:generated 3)
    (operator ebnf-v1:finish-node 7)
    (operator ebnf-v1:project-elements 5)
    (operator bnf-v1:elements-nil 0)
    (operator ebnf-v1:elements-end 1)
    (operator EBNF:ElementsNil 0)
    (operator bnf-v1:elements-cons 2)
    (operator bnf-v1:literal 2)
    (operator let 3)
    (operator ebnf-v1:text-length 1)
    (operator EBNF:ElementsCons 2)
    (operator EBNF:Literal 3)
    (operator bnf-v1:text-nil 0)
    (operator bnf-v1:text-cons 2)
    (operator bnf-v1:reference 2)
    (operator ebnf-v1:project-reference 10)
    (operator ebnf-v1:reference-start 11)
    (operator ebnf-v1:wrap-reference 5)
    (operator EBNF:Reference 4)
    (operator EBNF:Rule 6)
    (operator EBNF:Group 6)
    (operator EBNF:OptionalAbsent 3)
    (operator EBNF:OptionalPresent 4)
    (operator EBNF:Repetition 5)
    (operator EBNF:IterationsNil 0)
    (operator EBNF:IterationsCons 2)
    (operator ebnf-v1:repeat-step 8)
    (operator ebnf-v1:repetition-reversed 5)
    (operator ebnf-v1:source-order 1)
    (operator ebnf-v1:iterations-source-order 2))
  (equations)
  (rewrites
    (rule project
      (head
        (metta-equation
          (ebnf-v1:project-derivations
            (bnf-v1:document ?entries ?span)
            ?origins
            (bnf-v1:grammar-authority (bnf-v1:start ?start) (bnf-v1:lexical-environment ?lexicals))
            ?trees)
          (ebnf-v1:project-roots
            (ebnf-v1:projection-context ?entries ?origins ?lexicals)
            ?start
            (langdef:expression->list ?trees))))
      (body))

    (rule roots-empty
      (head (metta-equation (ebnf-v1:project-roots ?context ?start LNil) (EBNF:DerivationsNil)))
      (body))

    (rule roots-cons
      (head
        (metta-equation
          (ebnf-v1:project-roots ?context ?start (LCons ?tree ?rest))
          (EBNF:DerivationsCons
            (ebnf-v1:source-order (ebnf-v1:project-tree ?context ?start ?tree))
            (ebnf-v1:project-roots ?context ?start ?rest))))
      (body))

    (rule tree
      (head
        (metta-equation
          (ebnf-v1:project-tree (ebnf-v1:projection-context ?entries ?origins ?lexicals) ?name ?tree)
          (ebnf-v1:project-found
            (ebnf-v1:projection-context ?entries ?origins ?lexicals)
            ?name
            (ebnf-v1:find-definition ?name ?entries ?lexicals)
            (langdef:expression->list ?tree))))
      (body))

    (rule definition-empty
      (head
        (metta-equation
          (ebnf-v1:find-definition ?name (bnf-v1:entries-nil) ?lexicals)
          (ebnf-v1:find-lexical ?name ?lexicals)))
      (body))

    (rule definition-rule
      (head
        (metta-equation
          (ebnf-v1:find-definition
            ?name
            (bnf-v1:entries-cons (bnf-v1:rule ?found ?expression ?span) ?rest)
            ?lexicals)
          (ebnf-v1:definition-choice
            (langdef:ground-equal ?name ?found)
            ?name
            ?expression
            ?span
            ?rest
            ?lexicals)))
      (body))

    (rule definition-yes
      (head
        (metta-equation
          (ebnf-v1:definition-choice LangDef:SameValue ?name ?expression ?span ?rest ?lexicals)
          (ebnf-v1:found-rule ?expression ?span)))
      (body))

    (rule definition-no
      (head
        (metta-equation
          (ebnf-v1:definition-choice LangDef:DifferentValue ?name ?expression ?span ?rest ?lexicals)
          (ebnf-v1:find-definition ?name ?rest ?lexicals)))
      (body))

    (rule definition-comment
      (head
        (metta-equation
          (ebnf-v1:find-definition ?name (bnf-v1:entries-cons (bnf-v1:comment ?text ?span) ?rest) ?lexicals)
          (ebnf-v1:find-definition ?name ?rest ?lexicals)))
      (body))

    (rule definition-blank
      (head
        (metta-equation
          (ebnf-v1:find-definition ?name (bnf-v1:entries-cons (bnf-v1:blank ?span) ?rest) ?lexicals)
          (ebnf-v1:find-definition ?name ?rest ?lexicals)))
      (body))

    (rule lexical-cons
      (head
        (metta-equation
          (ebnf-v1:find-lexical
            ?name
            (bnf-v1:lexical-declarations-cons
              (bnf-v1:lexical-declaration ?found ?class ?matcher ?label ?origin)
              ?rest))
          (ebnf-v1:lexical-choice (langdef:ground-equal ?name ?found) ?name ?label ?origin ?rest)))
      (body))

    (rule lexical-yes
      (head
        (metta-equation
          (ebnf-v1:lexical-choice LangDef:SameValue ?name ?label ?origin ?rest)
          (ebnf-v1:found-lexical ?label ?origin)))
      (body))

    (rule lexical-no
      (head
        (metta-equation
          (ebnf-v1:lexical-choice LangDef:DifferentValue ?name ?label ?origin ?rest)
          (ebnf-v1:find-lexical ?name ?rest)))
      (body))

    (rule found-rule
      (head
        (metta-equation
          (ebnf-v1:project-found
            (ebnf-v1:projection-context ?entries ?origins ?lexicals)
            ?name
            (ebnf-v1:found-rule (bnf-v1:expression ?alternatives ?expr-span) ?rule-span)
            (LCons CstRuleV1 (LCons ?label (LCons ?left (LCons ?right ?children)))))
          (ebnf-v1:project-alternative
            (ebnf-v1:projection-context ?entries ?origins ?lexicals)
            ?name
            ?rule-span
            (ebnf-v1:helper ?name ?origins 0)
            (ebnf-v1:select-alternative ?name ?label ?alternatives (bnf-v1:suffix-start) 0)
            ?left
            ?right
            ?children)))
      (body))

    (rule found-lexical
      (head
        (metta-equation
          (ebnf-v1:project-found
            ?context
            ?name
            (ebnf-v1:found-lexical ?expected ?origin)
            (LCons CstRuleV1 (LCons ?label (LCons ?left (LCons ?right (LCons (cp ?scalar) LNil))))))
          (ebnf-v1:checked-lexical
            (langdef:ground-equal ?expected ?label)
            (langdef:ground-equal (+ ?left 1) ?right)
            ?name
            ?label
            ?origin
            ?left
            ?right
            ?scalar)))
      (body))

    (rule checked-lexical
      (head
        (metta-equation
          (ebnf-v1:checked-lexical
            LangDef:SameValue
            LangDef:SameValue
            ?name
            ?label
            ?origin
            ?left
            ?right
            ?scalar)
          (EBNF:Lexical (bnf-v1:text->string ?name) ?label ?origin (EBNF:InputSpan ?left ?right) ?scalar)))
      (body))

    (rule select-cons
      (head
        (metta-equation
          (ebnf-v1:select-alternative ?name ?label (bnf-v1:alternatives-cons ?alternative ?rest) ?suffix ?index)
          (ebnf-v1:alternative-choice
            (langdef:ground-equal (bnf-v1:text->string (bnf-v1:text-append ?name ?suffix)) ?label)
            ?name
            ?label
            ?alternative
            ?rest
            ?suffix
            ?index)))
      (body))

    (rule select-yes
      (head
        (metta-equation
          (ebnf-v1:alternative-choice
            LangDef:SameValue
            ?name
            ?label
            (bnf-v1:alternative ?elements ?span)
            ?rest
            ?suffix
            ?index)
          (ebnf-v1:selected-alternative ?index ?elements ?span)))
      (body))

    (rule select-no
      (head
        (metta-equation
          (ebnf-v1:alternative-choice LangDef:DifferentValue ?name ?label ?alternative ?rest ?suffix ?index)
          (ebnf-v1:select-alternative ?name ?label ?rest (bnf-v1:suffix-next ?suffix) (+ ?index 1))))
      (body))

    (rule helper-empty
      (head (metta-equation (ebnf-v1:helper ?name (ebnf-v1:origins-nil) ?index) (ebnf-v1:authored)))
      (body))

    (rule helper-cons
      (head
        (metta-equation
          (ebnf-v1:helper ?name
            (ebnf-v1:origins-cons (ebnf-v1:helper-origin ?found ?kind ?owner ?span) ?rest) ?index)
          (ebnf-v1:helper-choice (langdef:ground-equal ?name ?found) ?name ?kind ?span ?rest ?index)))
      (body))

    (rule helper-yes
      (head
        (metta-equation
          (ebnf-v1:helper-choice LangDef:SameValue ?name ?kind ?span ?rest ?index)
          (ebnf-v1:generated ?index ?kind ?span)))
      (body))

    (rule helper-no
      (head
        (metta-equation
          (ebnf-v1:helper-choice LangDef:DifferentValue ?name ?kind ?span ?rest ?index)
          (ebnf-v1:helper ?name ?rest (+ ?index 1))))
      (body))

    (rule project-alternative
      (head
        (metta-equation
          (ebnf-v1:project-alternative
            ?context
            ?name
            ?rule-span
            ?origin
            (ebnf-v1:selected-alternative ?index ?elements ?alternative-span)
            ?left
            ?right
            ?children)
          (ebnf-v1:finish-node
            ?origin
            ?name
            ?rule-span
            ?index
            ?alternative-span
            (EBNF:InputSpan ?left ?right)
            (ebnf-v1:project-elements ?context ?elements ?children ?left ?right))))
      (body))

    (rule elements-empty
      (head
        (metta-equation
          (ebnf-v1:project-elements ?context (bnf-v1:elements-nil) LNil ?position ?end)
          (ebnf-v1:elements-end (langdef:ground-equal ?position ?end))))
      (body))

    (rule elements-end
      (head (metta-equation (ebnf-v1:elements-end LangDef:SameValue) (EBNF:ElementsNil)))
      (body))

    (rule elements-literal
      (head
        (metta-equation
          (ebnf-v1:project-elements
            ?context
            (bnf-v1:elements-cons (bnf-v1:literal ?text ?span) ?rest)
            ?children
            ?position
            ?end)
          (let
            ?next
            (+ ?position (ebnf-v1:text-length ?text))
            (EBNF:ElementsCons
              (EBNF:Literal (bnf-v1:text->string ?text) ?span (EBNF:InputSpan ?position ?next))
              (ebnf-v1:project-elements ?context ?rest ?children ?next ?end)))))
      (body))

    (rule length-empty
      (head (metta-equation (ebnf-v1:text-length (bnf-v1:text-nil)) 0))
      (body))

    (rule length-cons
      (head
        (metta-equation
          (ebnf-v1:text-length (bnf-v1:text-cons ?scalar ?rest))
          (+ 1 (ebnf-v1:text-length ?rest))))
      (body))

    (rule elements-reference
      (head
        (metta-equation
          (ebnf-v1:project-elements
            (ebnf-v1:projection-context ?entries ?origins ?lexicals)
            (bnf-v1:elements-cons (bnf-v1:reference ?name ?span) ?rest)
            (LCons ?tree ?children)
            ?position
            ?end)
          (ebnf-v1:project-reference
            (ebnf-v1:projection-context ?entries ?origins ?lexicals)
            ?name
            ?span
            (ebnf-v1:helper ?name ?origins 0)
            ?rest
            ?children
            ?position
            ?end
            ?tree
            (langdef:expression->list ?tree))))
      (body))

    (rule project-reference
      (head
        (metta-equation
          (ebnf-v1:project-reference
            ?context
            ?name
            ?span
            ?origin
            ?rest
            ?children
            ?position
            ?end
            ?tree
            (LCons CstRuleV1 (LCons ?label (LCons ?left (LCons ?right ?inner)))))
          (ebnf-v1:reference-start
            (langdef:ground-equal ?position ?left)
            ?context
            ?name
            ?span
            ?origin
            ?rest
            ?children
            ?left
            ?right
            ?end
            ?tree)))
      (body))

    (rule reference-start
      (head
        (metta-equation
          (ebnf-v1:reference-start
            LangDef:SameValue
            ?context
            ?name
            ?span
            ?origin
            ?rest
            ?children
            ?left
            ?right
            ?end
            ?tree)
          (EBNF:ElementsCons
            (ebnf-v1:wrap-reference
              ?origin
              ?name
              ?span
              (EBNF:InputSpan ?left ?right)
              (ebnf-v1:project-tree ?context ?name ?tree))
            (ebnf-v1:project-elements ?context ?rest ?children ?right ?end))))
      (body))

    (rule wrap-authored
      (head
        (metta-equation
          (ebnf-v1:wrap-reference (ebnf-v1:authored) ?name ?span ?input ?tree)
          (EBNF:Reference (bnf-v1:text->string ?name) ?span ?input ?tree)))
      (body))

    (rule wrap-helper
      (head
        (metta-equation
          (ebnf-v1:wrap-reference (ebnf-v1:generated ?index ?kind ?span) ?name ?reference-span ?input ?tree)
          ?tree))
      (body))

    (rule node-authored
      (head
        (metta-equation
          (ebnf-v1:finish-node (ebnf-v1:authored) ?name ?rule-span ?index ?alternative-span ?input ?elements)
          (EBNF:Rule (bnf-v1:text->string ?name) ?rule-span ?index ?alternative-span ?input ?elements)))
      (body))

    (rule node-group
      (head
        (metta-equation
          (ebnf-v1:finish-node
            (ebnf-v1:generated ?occurrence group ?span)
            ?name
            ?rule-span
            ?index
            ?alternative-span
            ?input
            ?elements)
          (EBNF:Group ?occurrence ?span ?index ?alternative-span ?input ?elements)))
      (body))

    (rule node-absent
      (head
        (metta-equation
          (ebnf-v1:finish-node
            (ebnf-v1:generated ?occurrence optional ?span)
            ?name
            ?rule-span
            0
            ?alternative-span
            ?input
            (EBNF:ElementsNil))
          (EBNF:OptionalAbsent ?occurrence ?span ?input)))
      (body))

    (rule node-present
      (head
        (metta-equation
          (ebnf-v1:finish-node
            (ebnf-v1:generated ?occurrence optional ?span)
            ?name
            ?rule-span
            1
            ?alternative-span
            ?input
            (EBNF:ElementsCons ?body (EBNF:ElementsNil)))
          (EBNF:OptionalPresent ?occurrence ?span ?input ?body)))
      (body))

    (rule node-star-empty
      (head
        (metta-equation
          (ebnf-v1:finish-node
            (ebnf-v1:generated ?occurrence zero-or-more ?span)
            ?name
            ?rule-span
            0
            ?alternative-span
            ?input
            (EBNF:ElementsNil))
          (ebnf-v1:repetition-reversed zero-or-more ?occurrence ?span ?input (EBNF:IterationsNil))))
      (body))

    (rule node-plus-last
      (head
        (metta-equation
          (ebnf-v1:finish-node
            (ebnf-v1:generated ?occurrence one-or-more ?span)
            ?name
            ?rule-span
            0
            ?alternative-span
            ?input
            (EBNF:ElementsCons ?body (EBNF:ElementsNil)))
          (ebnf-v1:repetition-reversed
            one-or-more
            ?occurrence
            ?span
            ?input
            (EBNF:IterationsCons ?body (EBNF:IterationsNil)))))
      (body))

    (rule node-zero-or-more-step
      (head
        (metta-equation
          (ebnf-v1:finish-node
            (ebnf-v1:generated ?occurrence zero-or-more ?span)
            ?name
            ?rule-span
            1
            ?alternative-span
            ?input
            (EBNF:ElementsCons
              (ebnf-v1:repetition-reversed zero-or-more ?tail-occurrence ?tail-span ?tail-input ?tail)
              (EBNF:ElementsCons
                ?body
                (EBNF:ElementsNil))))
          (ebnf-v1:repeat-step
            (langdef:ground-equal ?occurrence ?tail-occurrence)
            (langdef:ground-equal ?span ?tail-span)
            zero-or-more
            ?occurrence
            ?span
            ?input
            ?body
            ?tail)))
      (body))

    (rule node-one-or-more-step
      (head
        (metta-equation
          (ebnf-v1:finish-node
            (ebnf-v1:generated ?occurrence one-or-more ?span)
            ?name
            ?rule-span
            1
            ?alternative-span
            ?input
            (EBNF:ElementsCons
              (ebnf-v1:repetition-reversed one-or-more ?tail-occurrence ?tail-span ?tail-input ?tail)
              (EBNF:ElementsCons
                ?body
                (EBNF:ElementsNil))))
          (ebnf-v1:repeat-step
            (langdef:ground-equal ?occurrence ?tail-occurrence)
            (langdef:ground-equal ?span ?tail-span)
            one-or-more
            ?occurrence
            ?span
            ?input
            ?body
            ?tail)))
      (body))

    (rule repeat-step
      (head
        (metta-equation
          (ebnf-v1:repeat-step LangDef:SameValue LangDef:SameValue ?kind ?occurrence ?span ?input ?body ?tail)
          (ebnf-v1:repetition-reversed ?kind ?occurrence ?span ?input (EBNF:IterationsCons ?body ?tail))))
      (body))

    (rule source-order-rule
      (head (metta-equation
        (ebnf-v1:source-order (EBNF:Rule ?name ?rule-span ?choice ?alternative-span ?input ?elements))
        (EBNF:Rule ?name ?rule-span ?choice ?alternative-span ?input (ebnf-v1:source-order ?elements))))
      (body))
    (rule source-order-group
      (head (metta-equation
        (ebnf-v1:source-order (EBNF:Group ?occurrence ?span ?choice ?alternative-span ?input ?elements))
        (EBNF:Group ?occurrence ?span ?choice ?alternative-span ?input (ebnf-v1:source-order ?elements))))
      (body))
    (rule source-order-reference
      (head (metta-equation
        (ebnf-v1:source-order (EBNF:Reference ?name ?span ?input ?tree))
        (EBNF:Reference ?name ?span ?input (ebnf-v1:source-order ?tree))))
      (body))
    (rule source-order-elements-empty
      (head (metta-equation (ebnf-v1:source-order (EBNF:ElementsNil)) (EBNF:ElementsNil)))
      (body))
    (rule source-order-elements-cons
      (head (metta-equation
        (ebnf-v1:source-order (EBNF:ElementsCons ?head ?tail))
        (EBNF:ElementsCons (ebnf-v1:source-order ?head) (ebnf-v1:source-order ?tail))))
      (body))
    (rule source-order-absent
      (head (metta-equation
        (ebnf-v1:source-order (EBNF:OptionalAbsent ?occurrence ?span ?input))
        (EBNF:OptionalAbsent ?occurrence ?span ?input)))
      (body))
    (rule source-order-present
      (head (metta-equation
        (ebnf-v1:source-order (EBNF:OptionalPresent ?occurrence ?span ?input ?body))
        (EBNF:OptionalPresent ?occurrence ?span ?input (ebnf-v1:source-order ?body))))
      (body))
    (rule source-order-literal
      (head (metta-equation
        (ebnf-v1:source-order (EBNF:Literal ?text ?span ?input))
        (EBNF:Literal ?text ?span ?input)))
      (body))
    (rule source-order-lexical
      (head (metta-equation
        (ebnf-v1:source-order (EBNF:Lexical ?name ?label ?origin ?input ?scalar))
        (EBNF:Lexical ?name ?label ?origin ?input ?scalar)))
      (body))
    (rule source-order-repetition
      (head (metta-equation
        (ebnf-v1:source-order (ebnf-v1:repetition-reversed ?kind ?occurrence ?span ?input ?reverse))
        (EBNF:Repetition ?kind ?occurrence ?span ?input
          (ebnf-v1:iterations-source-order ?reverse (EBNF:IterationsNil)))))
      (body))
    (rule iterations-source-order-empty
      (head (metta-equation (ebnf-v1:iterations-source-order (EBNF:IterationsNil) ?tail) ?tail))
      (body))
    (rule iterations-source-order-cons
      (head (metta-equation
        (ebnf-v1:iterations-source-order (EBNF:IterationsCons ?head ?rest) ?tail)
        (ebnf-v1:iterations-source-order ?rest
          (EBNF:IterationsCons (ebnf-v1:source-order ?head) ?tail))))
      (body))))
