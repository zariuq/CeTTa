(gslt-presentation-v1 TokenParserCompilerV1
  (signature
    (operator metta-equation 2)
    (operator ldpt-v1:compile-rule 1)
    (operator ldpt-v1:finish-rule 4)
    (operator ldpt-v1:compile-syntax 3)
    (operator ldpt-v1:prepend-keyword 2)
    (operator ldpt-v1:compile-reference 5)
    (operator ldpt-v1:prepend-reference 3)
    (operator ldpt-v1:compile-entry 1)
    (operator ldpt-v1:compile-layout 1)
    (operator ldpp-v1:text-equal 2)
    (operator ldpp-v1:parts 2)
    (operator ParserProductionInputV1 2)
    (operator GrammarRule 5)
    (operator TermSimple 2)
    (operator TBase 1)
    (operator SyntaxTerminal 1)
    (operator SyntaxNonTerminal 1)
    (operator EvalNone 0)
    (operator LNil 0)
    (operator LCons 2)
    (operator bnf-v1:text-nil 0)
    (operator bnf-v1:text-cons 2)
    (operator True 0)
    (operator False 0)
    (operator pp-def 1)
    (operator pp-entry 1)
    (operator pp-layout 0)
    (operator pp-trivia 0)
    (operator pp-keyword 1)
    (operator pp-occurrence 1)
    (operator pp-label 2)
    (operator pp-production 4)
    (operator pp-items-nil 0)
    (operator pp-items-cons 2)
    (operator pp-terminal 1)
    (operator pp-terminal-eof 0)
    (operator pp-nonterminal 1)
    (operator pa-apply 2)
    (operator pa-nil 0)
    (operator pa-cons 2)
    (operator pa-const 1)
    (operator pa-slot 1)
    (operator CstRuleV1 0)
    (operator TokenLayoutV1 0)
    (operator q-zero 0)
    (operator q-succ 1))
  (equations)
  (rewrites
    (rule token-compile-rule
      (head (metta-equation
        (ldpt-v1:compile-rule
          (ParserProductionInputV1 ?occurrence
            (GrammarRule ?label ?category ?params ?syntax EvalNone)))
        (ldpt-v1:finish-rule ?occurrence ?label ?category
          (ldpt-v1:compile-syntax ?params ?syntax q-zero))))
      (body))
    (rule token-finish-rule
      (head (metta-equation
        (ldpt-v1:finish-rule ?occurrence ?label ?category
          (ldpp-v1:parts ?items ?actions))
        (pp-production (pp-occurrence ?occurrence) (pp-def ?category)
          ?items (pa-apply CstRuleV1
            (pa-cons (pa-const ?label) ?actions)))))
      (body))
    (rule token-syntax-empty
      (head (metta-equation
        (ldpt-v1:compile-syntax LNil LNil ?slot)
        (ldpp-v1:parts pp-items-nil pa-nil)))
      (body))
    (rule token-syntax-empty-terminal
      (head (metta-equation
        (ldpt-v1:compile-syntax ?params
          (LCons (SyntaxTerminal (bnf-v1:text-nil)) ?syntax) ?slot)
        (ldpt-v1:compile-syntax ?params ?syntax ?slot)))
      (body))
    (rule token-syntax-keyword
      (head (metta-equation
        (ldpt-v1:compile-syntax ?params
          (LCons (SyntaxTerminal (bnf-v1:text-cons ?scalar ?tail)) ?syntax) ?slot)
        (ldpt-v1:prepend-keyword (bnf-v1:text-cons ?scalar ?tail)
          (ldpt-v1:compile-syntax ?params ?syntax (q-succ ?slot)))))
      (body))
    (rule token-prepend-keyword
      (head (metta-equation
        (ldpt-v1:prepend-keyword ?text (ldpp-v1:parts ?items ?actions))
        (ldpp-v1:parts
          (pp-items-cons (pp-nonterminal (pp-keyword ?text)) ?items) ?actions)))
      (body))
    (rule token-syntax-reference
      (head (metta-equation
        (ldpt-v1:compile-syntax
          (LCons (TermSimple ?name (TBase ?category)) ?params)
          (LCons (SyntaxNonTerminal ?reference) ?syntax) ?slot)
        (ldpt-v1:compile-reference (ldpp-v1:text-equal ?name ?reference)
          ?category ?slot ?params ?syntax)))
      (body))
    (rule token-reference-matching
      (head (metta-equation
        (ldpt-v1:compile-reference True ?category ?slot ?params ?syntax)
        (ldpt-v1:prepend-reference ?category ?slot
          (ldpt-v1:compile-syntax ?params ?syntax (q-succ ?slot)))))
      (body))
    (rule token-prepend-reference
      (head (metta-equation
        (ldpt-v1:prepend-reference ?category ?slot
          (ldpp-v1:parts ?items ?actions))
        (ldpp-v1:parts
          (pp-items-cons (pp-nonterminal (pp-def ?category)) ?items)
          (pa-cons (pa-slot ?slot) ?actions))))
      (body))
    (rule token-compile-entry
      (head (metta-equation
        (ldpt-v1:compile-entry ?start)
        (pp-production
          (pp-label (pp-entry (pp-def ?start)) "token-parser:whole-source")
          (pp-entry (pp-def ?start))
          (pp-items-cons (pp-nonterminal (pp-def ?start))
            (pp-items-cons (pp-nonterminal pp-layout)
              (pp-items-cons (pp-terminal pp-terminal-eof) pp-items-nil)))
          (pa-slot q-zero))))
      (body))
    (rule token-compile-layout
      (head (metta-equation
        (ldpt-v1:compile-layout pp-layout)
        (LCons
          (pp-production (pp-label pp-layout "token-parser:layout-empty")
            pp-layout pp-items-nil (pa-const TokenLayoutV1))
          (LCons
            (pp-production (pp-label pp-layout "token-parser:layout-cons")
              pp-layout
              (pp-items-cons (pp-nonterminal pp-trivia)
                (pp-items-cons (pp-nonterminal pp-layout) pp-items-nil))
              (pa-const TokenLayoutV1)) LNil))))
      (body))))
