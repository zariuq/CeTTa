(gslt-presentation-v1 LanguageDefParserCompilerV1
  (signature
    (operator metta-equation 2)
    (operator ldpp-v1:compile-rule 1)
    (operator ldpp-v1:compile-entry 1)
    (operator ldpp-v1:compile-class 1)
    (operator ldpp-v1:compile-lexical 1)
    (operator ldpp-v1:compile-syntax 3)
    (operator ldpp-v1:compile-literal 4)
    (operator ldpp-v1:compile-reference 5)
    (operator ldpp-v1:prepend-terminal 2)
    (operator ldpp-v1:prepend-reference 3)
    (operator ldpp-v1:finish-rule 3)
    (operator ldpp-v1:compile-points 2)
    (operator ldpp-v1:compile-exclusions 1)
    (operator ldpp-v1:text-equal 2)
    (operator ldpp-v1:parts 2)
    (operator GrammarRule 5)
    (operator TermSimple 2)
    (operator TBase 1)
    (operator SyntaxTerminal 1)
    (operator SyntaxNonTerminal 1)
    (operator EvalNone 0)
    (operator LexicalClassPoints 2)
    (operator LexicalClassExcept 2)
    (operator LexicalState 3)
    (operator LNil 0)
    (operator LCons 2)
    (operator bnf-v1:text-nil 0)
    (operator bnf-v1:text-cons 2)
    (operator True 0)
    (operator False 0)
    (operator cp 1)
    (operator pp-def 1)
    (operator pp-entry 1)
    (operator pp-class 1)
    (operator pp-label 2)
    (operator pp-production 4)
    (operator pp-items-nil 0)
    (operator pp-items-cons 2)
    (operator pp-terminal 1)
    (operator pp-terminal-char 1)
    (operator pp-terminal-class 1)
    (operator pp-terminal-eof 0)
    (operator pp-nonterminal 1)
    (operator pp-class-point 2)
    (operator pp-class-except 2)
    (operator pp-points-nil 0)
    (operator pp-points-cons 2)
    (operator pa-apply 2)
    (operator pa-nil 0)
    (operator pa-cons 2)
    (operator pa-const 1)
    (operator pa-slot 1)
    (operator CstRuleV1 0)
    (operator q-zero 0)
    (operator q-succ 1))
  (equations)
  (rewrites
    (rule compile-rule
      (head (metta-equation
        (ldpp-v1:compile-rule
          (GrammarRule ?label ?category ?params ?syntax EvalNone))
        (ldpp-v1:finish-rule ?label ?category
          (ldpp-v1:compile-syntax ?params ?syntax q-zero))))
      (body))

    (rule finish-rule
      (head (metta-equation
        (ldpp-v1:finish-rule
          ?label ?category (ldpp-v1:parts ?items ?actions))
        (pp-production
          (pp-label (pp-def ?category) ?label)
          (pp-def ?category)
          ?items
          (pa-apply CstRuleV1
            (pa-cons (pa-const ?label) ?actions)))))
      (body))

    (rule compile-syntax-empty
      (head (metta-equation
        (ldpp-v1:compile-syntax LNil LNil ?slot)
        (ldpp-v1:parts pp-items-nil pa-nil)))
      (body))

    (rule compile-syntax-terminal
      (head (metta-equation
        (ldpp-v1:compile-syntax
          ?params (LCons (SyntaxTerminal ?text) ?syntax) ?slot)
        (ldpp-v1:compile-literal ?params ?text ?syntax ?slot)))
      (body))

    (rule compile-literal-empty
      (head (metta-equation
        (ldpp-v1:compile-literal
          ?params (bnf-v1:text-nil) ?syntax ?slot)
        (ldpp-v1:compile-syntax ?params ?syntax ?slot)))
      (body))

    (rule compile-literal-scalar
      (head (metta-equation
        (ldpp-v1:compile-literal
          ?params (bnf-v1:text-cons ?scalar ?tail) ?syntax ?slot)
        (ldpp-v1:prepend-terminal ?scalar
          (ldpp-v1:compile-literal
            ?params ?tail ?syntax (q-succ ?slot)))))
      (body))

    (rule prepend-terminal
      (head (metta-equation
        (ldpp-v1:prepend-terminal
          ?scalar (ldpp-v1:parts ?items ?actions))
        (ldpp-v1:parts
          (pp-items-cons
            (pp-terminal (pp-terminal-char (cp ?scalar))) ?items)
          ?actions)))
      (body))

    (rule compile-syntax-reference
      (head (metta-equation
        (ldpp-v1:compile-syntax
          (LCons (TermSimple ?name (TBase ?category)) ?params)
          (LCons (SyntaxNonTerminal ?reference) ?syntax)
          ?slot)
        (ldpp-v1:compile-reference
          (ldpp-v1:text-equal ?name ?reference)
          ?category ?slot ?params ?syntax)))
      (body))

    (rule compile-reference-matching-name
      (head (metta-equation
        (ldpp-v1:compile-reference True ?category ?slot ?params ?syntax)
        (ldpp-v1:prepend-reference ?category ?slot
          (ldpp-v1:compile-syntax ?params ?syntax (q-succ ?slot)))))
      (body))

    (rule prepend-reference
      (head (metta-equation
        (ldpp-v1:prepend-reference
          ?category ?slot (ldpp-v1:parts ?items ?actions))
        (ldpp-v1:parts
          (pp-items-cons (pp-nonterminal (pp-def ?category)) ?items)
          (pa-cons (pa-slot ?slot) ?actions))))
      (body))

    (rule compile-entry
      (head (metta-equation
        (ldpp-v1:compile-entry ?start)
        (pp-production
          (pp-label
            (pp-entry (pp-def ?start))
            "language-def-parser-pack:whole-source")
          (pp-entry (pp-def ?start))
          (pp-items-cons
            (pp-nonterminal (pp-def ?start))
            (pp-items-cons
              (pp-terminal pp-terminal-eof) pp-items-nil))
          (pa-slot q-zero))))
      (body))

    (rule compile-class-points
      (head (metta-equation
        (ldpp-v1:compile-class (LexicalClassPoints ?name ?points))
        (ldpp-v1:compile-points ?name ?points)))
      (body))

    (rule compile-points-empty
      (head (metta-equation
        (ldpp-v1:compile-points ?name LNil)
        LNil))
      (body))

    (rule compile-points-cons
      (head (metta-equation
        (ldpp-v1:compile-points ?name (LCons ?scalar ?tail))
        (LCons
          (pp-class-point (pp-class ?name) (cp ?scalar))
          (ldpp-v1:compile-points ?name ?tail))))
      (body))

    (rule compile-class-except
      (head (metta-equation
        (ldpp-v1:compile-class (LexicalClassExcept ?name ?excluded))
        (LCons
          (pp-class-except (pp-class ?name)
            (ldpp-v1:compile-exclusions ?excluded))
          LNil)))
      (body))

    (rule compile-exclusions-empty
      (head (metta-equation
        (ldpp-v1:compile-exclusions LNil)
        pp-points-nil))
      (body))

    (rule compile-exclusions-cons
      (head (metta-equation
        (ldpp-v1:compile-exclusions (LCons ?scalar ?tail))
        (pp-points-cons
          (cp ?scalar) (ldpp-v1:compile-exclusions ?tail))))
      (body))

    (rule compile-lexical
      (head (metta-equation
        (ldpp-v1:compile-lexical (LexicalState ?sort ?class ?label))
        (pp-production
          (pp-label (pp-def ?sort) ?label)
          (pp-def ?sort)
          (pp-items-cons
            (pp-terminal (pp-terminal-class (pp-class ?class)))
            pp-items-nil)
          (pa-apply CstRuleV1
            (pa-cons (pa-const ?label)
              (pa-cons (pa-slot q-zero) pa-nil))))))
      (body))))
