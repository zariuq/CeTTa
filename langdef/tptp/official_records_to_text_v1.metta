; Canonical TPTP/TSTP text from the compact public value language.
; Layout and redundant parentheses are normalized; source text is metadata.
(gslt-presentation-v1 TptpOfficialRecordsToTextV1
  (signature
    (operator metta-equation 2)
    (operator langdef:ground-equal 2)
    (operator langdef:value-view 1)
    (operator str:byte-length 1) (operator str:byte-slice 3)
    (operator str:concat 2) (operator str:escape 2)
    (operator str:starts-with 2) (operator str:string->symbol 1)
    (operator str:string->number 1)
    (operator langdef:list->expression 1)
    (operator langdef:list->list-value 1)
    (operator bnf-v1:string->text 1) (operator bnf-v1:text->string 1)
    (operator bnf-v1:text-append 2) (operator bnf-v1:text-nil 0)
    (operator LNil 0) (operator LCons 2)
    (operator LangDef:SameValue 0) (operator LangDef:DifferentValue 0)
    (operator LangDef:ExpressionValue 1)
    (operator LangDef:ListValue 1)
    (operator LangDef:SymbolValue 1)
    (operator LangDef:StringValue 1)
    (operator LangDef:IntegerValue 1)

    (operator fof 0) (operator cnf 0) (operator tff 0)
    (operator thf 0) (operator tcf 0) (operator tpi 0)
    (operator incl 0) (operator annotation 0)
    (operator inference 0) (operator file 0)
    (operator introduced 0) (operator creator 0) (operator theory 0)
    (operator ∀ 0) (operator ∃ 0) (operator λ 0)
    (operator Π 0) (operator Σ 0)
    (operator choice 0) (operator definite 0)
    (operator ¬ 0) (operator ∧ 0) (operator ∨ 0)
    (operator → 0) (operator ← 0) (operator ↔ 0)
    (operator ⊕ 0) (operator ⊼ 0) (operator ⊽ 0)
    (operator ≈ 0) (operator ≉ 0) (operator ≡ 0)
    (operator : 0) (operator ⇒ 0) (operator × 0) (operator ⊎ 0)
    (operator ℚ 0) (operator ℝ 0) (operator ◊ 0) (operator ⌜ 0)
    (operator §let 0) (operator ≔ 0) (operator ⟨⟩ 0)

    (operator TptpPrint:FO 0) (operator TptpPrint:TF 0)
    (operator TptpPrint:TH 0) (operator TptpPrint:Type 0)
    (operator TptpPrint:General 0)
    (operator TptpPrint:Special 0)
    (operator TptpPrint:Application 0)

    (operator tptp-print-v1:document 1)
    (operator tptp-print-v1:document-view 1)
    (operator tptp-print-v1:inputs 1)
    (operator tptp-print-v1:input 1)
    (operator tptp-print-v1:input-view 1)
    (operator tptp-print-v1:input-list 1)
    (operator tptp-print-v1:annotated 6)
    (operator tptp-print-v1:annotation-tail 1)
    (operator tptp-print-v1:annotation-tail-view 1)
    (operator tptp-print-v1:annotation-tail-list 1)
    (operator tptp-print-v1:annotation-tail-choice 3)
    (operator tptp-print-v1:annotation-parts 1)
    (operator tptp-print-v1:name 1)
    (operator tptp-print-v1:role 1)

    (operator tptp-print-v1:expr 2)
    (operator tptp-print-v1:expr-view 2)
    (operator tptp-print-v1:expr-list 2)
    (operator tptp-print-v1:expr-head-view 3)
    (operator tptp-print-v1:expr-head-class 4)
    (operator tptp-print-v1:special-heads 0)
    (operator tptp-print-v1:head-class 1)
    (operator tptp-print-v1:head-member 2)
    (operator tptp-print-v1:head-member-next 3)
    (operator tptp-print-v1:special 3)
    (operator tptp-print-v1:atom 1)
    (operator tptp-print-v1:atom-view 1)
    (operator tptp-print-v1:symbol-text 1)
    (operator tptp-print-v1:symbol-first 2)
    (operator tptp-print-v1:symbol-section 1)
    (operator tptp-print-v1:symbol-section-next 2)
    (operator tptp-print-v1:after 2)
    (operator tptp-print-v1:quoted 3)

    (operator tptp-print-v1:call 3)
    (operator tptp-print-v1:call-next 4)
    (operator tptp-print-v1:ordinary-call 2)
    (operator tptp-print-v1:th-call 2)
    (operator tptp-print-v1:th-arguments 1)
    (operator tptp-print-v1:items 2)
    (operator tptp-print-v1:brackets 2)
    (operator tptp-print-v1:brackets-view 2)
    (operator tptp-print-v1:binary 4)
    (operator tptp-print-v1:infix 4)
    (operator tptp-print-v1:infix-operand 2)
    (operator tptp-print-v1:infix-operand-view 2)
    (operator tptp-print-v1:infix-operand-class 3)
    (operator tptp-print-v1:grouped-heads 0)
    (operator tptp-print-v1:binary-assoc 4)
    (operator tptp-print-v1:binary-chain 4)
    (operator tptp-print-v1:binary-left 4)
    (operator tptp-print-v1:binary-left-view 4)
    (operator tptp-print-v1:binary-left-next 6)
    (operator tptp-print-v1:unary 3)
    (operator tptp-print-v1:quantified 4)
    (operator tptp-print-v1:variables 1)
    (operator tptp-print-v1:variables-view 1)
    (operator tptp-print-v1:type-binary 3)
    (operator tptp-print-v1:tuple 2)
    (operator tptp-print-v1:raw-string 1)
    (operator tptp-print-v1:raw-string-view 1)
    (operator tptp-print-v1:let-component 3)
    (operator tptp-print-v1:let-component-view 3)
    (operator tptp-print-v1:let-component-list 3)
    (operator tptp-print-v1:let-component-head 4)
    (operator tptp-print-v1:let-component-choice 3)

    (operator tptp-print-v1:cat2 2)
    (operator tptp-print-v1:cat3 3)
    (operator tptp-print-v1:cat4 4)
    (operator tptp-print-v1:cat5 5))
  (equations)
  (rewrites
    (rule cat2 (head (metta-equation
      (tptp-print-v1:cat2 ?a ?b)
      (bnf-v1:text-append ?a ?b))) (body))
    (rule cat3 (head (metta-equation
      (tptp-print-v1:cat3 ?a ?b ?c)
      (bnf-v1:text-append ?a (bnf-v1:text-append ?b ?c)))) (body))
    (rule cat4 (head (metta-equation
      (tptp-print-v1:cat4 ?a ?b ?c ?d)
      (bnf-v1:text-append ?a
        (bnf-v1:text-append ?b (bnf-v1:text-append ?c ?d))))) (body))
    (rule cat5 (head (metta-equation
      (tptp-print-v1:cat5 ?a ?b ?c ?d ?e)
      (bnf-v1:text-append ?a
        (bnf-v1:text-append ?b
          (bnf-v1:text-append ?c (bnf-v1:text-append ?d ?e)))))) (body))

    (rule document (head (metta-equation
      (tptp-print-v1:document ?records)
      (bnf-v1:text->string
        (tptp-print-v1:document-view
          (langdef:value-view ?records))))) (body))
    (rule document-expression (head (metta-equation
      (tptp-print-v1:document-view (LangDef:ExpressionValue ?records))
      (tptp-print-v1:inputs ?records))) (body))
    (rule document-list (head (metta-equation
      (tptp-print-v1:document-view (LangDef:ListValue ?records))
      (tptp-print-v1:inputs ?records))) (body))
    (rule inputs-empty (head (metta-equation
      (tptp-print-v1:inputs LNil)
      (bnf-v1:text-nil))) (body))
    (rule inputs-cons (head (metta-equation
      (tptp-print-v1:inputs (LCons ?record ?rest))
      (tptp-print-v1:cat3
        (tptp-print-v1:input ?record)
        (bnf-v1:string->text "\n")
        (tptp-print-v1:inputs ?rest)))) (body))
    (rule input (head (metta-equation
      (tptp-print-v1:input ?record)
      (tptp-print-v1:input-view
        (langdef:value-view ?record)))) (body))
    (rule input-expression (head (metta-equation
      (tptp-print-v1:input-view (LangDef:ExpressionValue ?fields))
      (tptp-print-v1:input-list ?fields))) (body))

    (rule input-fof (head (metta-equation
      (tptp-print-v1:input-list
        (LCons fof (LCons ?name (LCons ?role (LCons ?formula LNil)))))
      (tptp-print-v1:annotated
        "fof" TptpPrint:FO ?name ?role ?formula (bnf-v1:text-nil)))) (body))
    (rule input-fof-annotation (head (metta-equation
      (tptp-print-v1:input-list
        (LCons fof
          (LCons ?name
            (LCons ?role (LCons ?formula (LCons ?ann LNil))))))
      (tptp-print-v1:annotated
        "fof" TptpPrint:FO ?name ?role ?formula
        (tptp-print-v1:annotation-tail ?ann)))) (body))
    (rule input-cnf (head (metta-equation
      (tptp-print-v1:input-list
        (LCons cnf (LCons ?name (LCons ?role (LCons ?formula LNil)))))
      (tptp-print-v1:annotated
        "cnf" TptpPrint:FO ?name ?role ?formula (bnf-v1:text-nil)))) (body))
    (rule input-cnf-annotation (head (metta-equation
      (tptp-print-v1:input-list
        (LCons cnf
          (LCons ?name
            (LCons ?role (LCons ?formula (LCons ?ann LNil))))))
      (tptp-print-v1:annotated
        "cnf" TptpPrint:FO ?name ?role ?formula
        (tptp-print-v1:annotation-tail ?ann)))) (body))
    (rule input-tff (head (metta-equation
      (tptp-print-v1:input-list
        (LCons tff (LCons ?name (LCons ?role (LCons ?formula LNil)))))
      (tptp-print-v1:annotated
        "tff" TptpPrint:TF ?name ?role ?formula (bnf-v1:text-nil)))) (body))
    (rule input-tff-annotation (head (metta-equation
      (tptp-print-v1:input-list
        (LCons tff
          (LCons ?name
            (LCons ?role (LCons ?formula (LCons ?ann LNil))))))
      (tptp-print-v1:annotated
        "tff" TptpPrint:TF ?name ?role ?formula
        (tptp-print-v1:annotation-tail ?ann)))) (body))
    (rule input-thf (head (metta-equation
      (tptp-print-v1:input-list
        (LCons thf (LCons ?name (LCons ?role (LCons ?formula LNil)))))
      (tptp-print-v1:annotated
        "thf" TptpPrint:TH ?name ?role ?formula (bnf-v1:text-nil)))) (body))
    (rule input-thf-annotation (head (metta-equation
      (tptp-print-v1:input-list
        (LCons thf
          (LCons ?name
            (LCons ?role (LCons ?formula (LCons ?ann LNil))))))
      (tptp-print-v1:annotated
        "thf" TptpPrint:TH ?name ?role ?formula
        (tptp-print-v1:annotation-tail ?ann)))) (body))
    (rule input-tcf (head (metta-equation
      (tptp-print-v1:input-list
        (LCons tcf (LCons ?name (LCons ?role (LCons ?formula LNil)))))
      (tptp-print-v1:annotated
        "tcf" TptpPrint:TF ?name ?role ?formula (bnf-v1:text-nil)))) (body))
    (rule input-tcf-annotation (head (metta-equation
      (tptp-print-v1:input-list
        (LCons tcf
          (LCons ?name
            (LCons ?role (LCons ?formula (LCons ?ann LNil))))))
      (tptp-print-v1:annotated
        "tcf" TptpPrint:TF ?name ?role ?formula
        (tptp-print-v1:annotation-tail ?ann)))) (body))
    (rule input-tpi (head (metta-equation
      (tptp-print-v1:input-list
        (LCons tpi (LCons ?name (LCons ?role (LCons ?formula LNil)))))
      (tptp-print-v1:annotated
        "tpi" TptpPrint:General ?name ?role ?formula
        (bnf-v1:text-nil)))) (body))
    (rule input-tpi-annotation (head (metta-equation
      (tptp-print-v1:input-list
        (LCons tpi
          (LCons ?name
            (LCons ?role (LCons ?formula (LCons ?ann LNil))))))
      (tptp-print-v1:annotated
        "tpi" TptpPrint:General ?name ?role ?formula
        (tptp-print-v1:annotation-tail ?ann)))) (body))
    (rule input-include (head (metta-equation
      (tptp-print-v1:input-list
        (LCons incl (LCons ?file LNil)))
      (tptp-print-v1:cat3
        (bnf-v1:string->text "include(")
        (tptp-print-v1:name ?file)
        (bnf-v1:string->text ").")))) (body))
    (rule input-include-selection (head (metta-equation
      (tptp-print-v1:input-list
        (LCons incl (LCons ?file (LCons ?selection LNil))))
      (tptp-print-v1:cat5
        (bnf-v1:string->text "include(")
        (tptp-print-v1:name ?file)
        (bnf-v1:string->text ",")
        (tptp-print-v1:brackets TptpPrint:General ?selection)
        (bnf-v1:string->text ").")))) (body))
    (rule input-include-qualified (head (metta-equation
      (tptp-print-v1:input-list
        (LCons incl
          (LCons ?file (LCons ?selection (LCons ?qualification LNil)))))
      (tptp-print-v1:cat5
        (bnf-v1:string->text "include(")
        (tptp-print-v1:name ?file)
        (bnf-v1:string->text ",")
        (tptp-print-v1:brackets TptpPrint:General ?selection)
        (tptp-print-v1:cat3
          (bnf-v1:string->text ",")
          (tptp-print-v1:name ?qualification)
          (bnf-v1:string->text ")."))))) (body))

    (rule annotated (head (metta-equation
      (tptp-print-v1:annotated
        ?tag ?mode ?name ?role ?formula ?tail)
      (tptp-print-v1:cat5
        (bnf-v1:string->text ?tag)
        (bnf-v1:string->text "(")
        (tptp-print-v1:name ?name)
        (bnf-v1:string->text ",")
        (tptp-print-v1:cat5
          (tptp-print-v1:role ?role)
          (bnf-v1:string->text ",")
          (tptp-print-v1:expr ?mode ?formula)
          ?tail
          (bnf-v1:string->text ")."))))) (body))

    (rule annotation-tail (head (metta-equation
      (tptp-print-v1:annotation-tail ?ann)
      (tptp-print-v1:annotation-tail-view (langdef:value-view ?ann)))) (body))
    (rule annotation-tail-expression (head (metta-equation
      (tptp-print-v1:annotation-tail-view (LangDef:ExpressionValue ?items))
      (tptp-print-v1:annotation-tail-list ?items))) (body))
    (rule annotation-tail-list-value (head (metta-equation
      (tptp-print-v1:annotation-tail-view (LangDef:ListValue ?items))
      (tptp-print-v1:cat2
        (bnf-v1:string->text ",")
        (tptp-print-v1:expr-view TptpPrint:General (LangDef:ListValue ?items))))) (body))
    (rule annotation-tail-symbol (head (metta-equation
      (tptp-print-v1:annotation-tail-view (LangDef:SymbolValue ?text))
      (tptp-print-v1:cat2
        (bnf-v1:string->text ",")
        (tptp-print-v1:expr-view TptpPrint:General (LangDef:SymbolValue ?text))))) (body))
    (rule annotation-tail-string (head (metta-equation
      (tptp-print-v1:annotation-tail-view (LangDef:StringValue ?text))
      (tptp-print-v1:cat2
        (bnf-v1:string->text ",")
        (tptp-print-v1:expr-view TptpPrint:General (LangDef:StringValue ?text))))) (body))
    (rule annotation-tail-integer (head (metta-equation
      (tptp-print-v1:annotation-tail-view (LangDef:IntegerValue ?text))
      (tptp-print-v1:cat2
        (bnf-v1:string->text ",")
        (tptp-print-v1:expr-view TptpPrint:General (LangDef:IntegerValue ?text))))) (body))
    (rule annotation-tail-empty (head (metta-equation
      (tptp-print-v1:annotation-tail-list LNil)
      (tptp-print-v1:cat2
        (bnf-v1:string->text ",")
        (tptp-print-v1:expr-view TptpPrint:General (LangDef:ExpressionValue LNil))))) (body))
    (rule annotation-tail-cons (head (metta-equation
      (tptp-print-v1:annotation-tail-list (LCons ?head ?rest))
      (tptp-print-v1:annotation-tail-choice
        (langdef:ground-equal ?head annotation) ?head ?rest))) (body))
    (rule annotation-tail-wrapper (head (metta-equation
      (tptp-print-v1:annotation-tail-choice
        LangDef:SameValue ?head ?rest)
      (tptp-print-v1:annotation-parts ?rest))) (body))
    (rule annotation-tail-direct (head (metta-equation
      (tptp-print-v1:annotation-tail-choice
        LangDef:DifferentValue ?head ?rest)
      (tptp-print-v1:cat2
        (bnf-v1:string->text ",")
        (tptp-print-v1:expr-view TptpPrint:General
          (LangDef:ExpressionValue (LCons ?head ?rest)))))) (body))
    (rule annotation-parts-two (head (metta-equation
      (tptp-print-v1:annotation-parts
        (LCons ?source (LCons ?info LNil)))
      (tptp-print-v1:cat4
        (bnf-v1:string->text ",")
        (tptp-print-v1:expr TptpPrint:General ?source)
        (bnf-v1:string->text ",")
        (tptp-print-v1:brackets TptpPrint:General ?info)))) (body))

    (rule name (head (metta-equation
      (tptp-print-v1:name ?name)
      (tptp-print-v1:atom ?name))) (body))
    (rule role (head (metta-equation
      (tptp-print-v1:role ?role)
      (tptp-print-v1:atom ?role))) (body))
    (rule atom (head (metta-equation
      (tptp-print-v1:atom ?value)
      (tptp-print-v1:atom-view
        (langdef:value-view ?value)))) (body))
    (rule atom-symbol (head (metta-equation
      (tptp-print-v1:atom-view (LangDef:SymbolValue ?spelling))
      (tptp-print-v1:symbol-text ?spelling))) (body))
    (rule atom-string (head (metta-equation
      (tptp-print-v1:atom-view (LangDef:StringValue ?string))
      (tptp-print-v1:quoted tptp-single "'" ?string))) (body))
    (rule atom-integer (head (metta-equation
      (tptp-print-v1:atom-view (LangDef:IntegerValue ?string))
      (bnf-v1:string->text ?string))) (body))

    ; §w is $w and §§w is $$w; § is two bytes.
    (rule symbol-text (head (metta-equation
      (tptp-print-v1:symbol-text ?spelling)
      (tptp-print-v1:symbol-first
        (str:starts-with ?spelling "§") ?spelling))) (body))
    (rule symbol-ordinary (head (metta-equation
      (tptp-print-v1:symbol-first False ?spelling)
      (bnf-v1:string->text ?spelling))) (body))
    (rule symbol-section (head (metta-equation
      (tptp-print-v1:symbol-first True ?spelling)
      (tptp-print-v1:symbol-section (tptp-print-v1:after ?spelling 2)))) (body))
    (rule symbol-section-next (head (metta-equation
      (tptp-print-v1:symbol-section ?rest)
      (tptp-print-v1:symbol-section-next
        (str:starts-with ?rest "§") ?rest))) (body))
    (rule symbol-section-defined (head (metta-equation
      (tptp-print-v1:symbol-section-next False ?rest)
      (bnf-v1:string->text (str:concat "$" ?rest)))) (body))
    (rule symbol-section-system (head (metta-equation
      (tptp-print-v1:symbol-section-next True ?rest)
      (bnf-v1:string->text
        (str:concat "$$" (tptp-print-v1:after ?rest 2))))) (body))
    (rule after (head (metta-equation
      (tptp-print-v1:after ?spelling ?count)
      (str:byte-slice ?spelling ?count (str:byte-length ?spelling)))) (body))

    ; A quoted text, with \ before each backslash and each quote.
    (rule quoted (head (metta-equation
      (tptp-print-v1:quoted ?scheme ?quote ?string)
      (bnf-v1:string->text
        (str:concat ?quote
          (str:concat (str:escape ?scheme ?string) ?quote))))) (body))

    (rule expr (head (metta-equation
      (tptp-print-v1:expr ?mode ?value)
      (tptp-print-v1:expr-view
        ?mode (langdef:value-view ?value)))) (body))
    (rule expr-symbol (head (metta-equation
      (tptp-print-v1:expr-view
        ?mode (LangDef:SymbolValue ?spelling))
      (tptp-print-v1:symbol-text ?spelling))) (body))
    (rule expr-string (head (metta-equation
      (tptp-print-v1:expr-view
        ?mode (LangDef:StringValue ?string))
      (tptp-print-v1:quoted tptp-single "'" ?string))) (body))
    (rule expr-integer (head (metta-equation
      (tptp-print-v1:expr-view
        ?mode (LangDef:IntegerValue ?string))
      (bnf-v1:string->text ?string))) (body))
    (rule expr-expression (head (metta-equation
      (tptp-print-v1:expr-view
        ?mode (LangDef:ExpressionValue ?items))
      (tptp-print-v1:expr-list ?mode ?items))) (body))
    (rule expr-list-value (head (metta-equation
      (tptp-print-v1:expr-view
        ?mode (LangDef:ListValue ?items))
      (tptp-print-v1:brackets-view ?mode (LangDef:ListValue ?items)))) (body))
    (rule expr-empty-list (head (metta-equation
      (tptp-print-v1:expr-list ?mode LNil)
      (bnf-v1:string->text "[]"))) (body))
    (rule expr-cons (head (metta-equation
      (tptp-print-v1:expr-list
        ?mode (LCons ?head ?arguments))
      (tptp-print-v1:expr-head-view
        ?mode ?arguments (langdef:value-view ?head)))) (body))
    (rule expr-symbol-head (head (metta-equation
      (tptp-print-v1:expr-head-view
        ?mode ?arguments (LangDef:SymbolValue ?spelling))
      (tptp-print-v1:expr-head-class
        (tptp-print-v1:head-class (str:string->symbol ?spelling))
        ?mode (str:string->symbol ?spelling) ?arguments))) (body))
    (rule expr-string-head (head (metta-equation
      (tptp-print-v1:expr-head-view
        ?mode ?arguments (LangDef:StringValue ?string))
      (tptp-print-v1:call ?mode ?string ?arguments))) (body))
    (rule expr-integer-head (head (metta-equation
      (tptp-print-v1:expr-head-view
        ?mode ?arguments (LangDef:IntegerValue ?string))
      (tptp-print-v1:brackets-view
        ?mode (LangDef:ExpressionValue
                (LCons (str:string->number ?string) ?arguments))))) (body))
    (rule expr-list-head (head (metta-equation
      (tptp-print-v1:expr-head-view
        ?mode ?arguments (LangDef:ListValue ?items))
      (tptp-print-v1:brackets-view
        ?mode (LangDef:ExpressionValue
                (LCons (langdef:list->list-value ?items) ?arguments))))) (body))
    (rule expr-expression-head (head (metta-equation
      (tptp-print-v1:expr-head-view
        ?mode ?arguments (LangDef:ExpressionValue ?items))
      (tptp-print-v1:brackets-view
        ?mode (LangDef:ExpressionValue
                (LCons (langdef:list->expression ?items) ?arguments))))) (body))
    (rule expr-special-head (head (metta-equation
      (tptp-print-v1:expr-head-class
        TptpPrint:Special ?mode ?head ?arguments)
      (tptp-print-v1:special ?mode ?head ?arguments))) (body))
    (rule expr-application-head (head (metta-equation
      (tptp-print-v1:expr-head-class
        TptpPrint:Application ?mode ?head ?arguments)
      (tptp-print-v1:call ?mode ?head ?arguments))) (body))

    (rule head-class (head (metta-equation
      (tptp-print-v1:head-class ?head)
      (tptp-print-v1:head-member
        ?head (tptp-print-v1:special-heads)))) (body))
    (rule special-heads (head (metta-equation
      (tptp-print-v1:special-heads)
      (LCons ¬
        (LCons ∧ (LCons ∨ (LCons → (LCons ← (LCons ↔
        (LCons ⊕ (LCons ⊼ (LCons ⊽
        (LCons ≈ (LCons ≉ (LCons ≡
        (LCons ∀ (LCons ∃ (LCons λ (LCons Π (LCons Σ
        (LCons choice (LCons definite
        (LCons : (LCons ⇒ (LCons × (LCons ⊎
        (LCons ℚ (LCons ℝ (LCons ◊ (LCons ⌜
        (LCons §let (LCons ≔ (LCons ⟨⟩
        (LCons inference (LCons file (LCons introduced
        (LCons creator (LCons theory LNil))))))))))))))))))))))))))))))))))))) (body))
    (rule head-member-empty (head (metta-equation
      (tptp-print-v1:head-member ?head LNil)
      TptpPrint:Application)) (body))
    (rule head-member-cons (head (metta-equation
      (tptp-print-v1:head-member
        ?head (LCons ?candidate ?rest))
      (tptp-print-v1:head-member-next
        (langdef:ground-equal ?head ?candidate) ?head ?rest))) (body))
    (rule head-member-found (head (metta-equation
      (tptp-print-v1:head-member-next
        LangDef:SameValue ?head ?rest)
      TptpPrint:Special)) (body))
    (rule head-member-more (head (metta-equation
      (tptp-print-v1:head-member-next
        LangDef:DifferentValue ?head ?rest)
      (tptp-print-v1:head-member ?head ?rest))) (body))

    (rule special-not (head (metta-equation
      (tptp-print-v1:special
        ?mode ¬ (LCons ?body LNil))
      (tptp-print-v1:unary ?mode "~" ?body))) (body))
    (rule special-and (head (metta-equation
      (tptp-print-v1:special ?mode ∧ ?arguments)
      (tptp-print-v1:binary-assoc ?mode "&" ∧ ?arguments))) (body))
    (rule special-or (head (metta-equation
      (tptp-print-v1:special ?mode ∨ ?arguments)
      (tptp-print-v1:binary-assoc ?mode "|" ∨ ?arguments))) (body))
    (rule special-implies (head (metta-equation
      (tptp-print-v1:special ?mode → ?arguments)
      (tptp-print-v1:binary ?mode "=>" → ?arguments))) (body))
    (rule special-reverse (head (metta-equation
      (tptp-print-v1:special ?mode ← ?arguments)
      (tptp-print-v1:binary ?mode "<=" ← ?arguments))) (body))
    (rule special-iff (head (metta-equation
      (tptp-print-v1:special ?mode ↔ ?arguments)
      (tptp-print-v1:binary ?mode "<=>" ↔ ?arguments))) (body))
    (rule special-xor (head (metta-equation
      (tptp-print-v1:special ?mode ⊕ ?arguments)
      (tptp-print-v1:binary ?mode "<~>" ⊕ ?arguments))) (body))
    (rule special-nand (head (metta-equation
      (tptp-print-v1:special ?mode ⊼ ?arguments)
      (tptp-print-v1:binary ?mode "~&" ⊼ ?arguments))) (body))
    (rule special-nor (head (metta-equation
      (tptp-print-v1:special ?mode ⊽ ?arguments)
      (tptp-print-v1:binary ?mode "~|" ⊽ ?arguments))) (body))
    (rule special-equals (head (metta-equation
      (tptp-print-v1:special ?mode ≈ ?arguments)
      (tptp-print-v1:infix ?mode "=" ≈ ?arguments))) (body))
    (rule special-not-equals (head (metta-equation
      (tptp-print-v1:special ?mode ≉ ?arguments)
      (tptp-print-v1:infix ?mode "!=" ≉ ?arguments))) (body))
    (rule special-identical (head (metta-equation
      (tptp-print-v1:special ?mode ≡ ?arguments)
      (tptp-print-v1:infix ?mode "==" ≡ ?arguments))) (body))
    (rule special-forall (head (metta-equation
      (tptp-print-v1:special
        ?mode ∀ (LCons ?variables (LCons ?body LNil)))
      (tptp-print-v1:quantified ?mode "!" ?variables ?body))) (body))
    (rule special-exists (head (metta-equation
      (tptp-print-v1:special
        ?mode ∃ (LCons ?variables (LCons ?body LNil)))
      (tptp-print-v1:quantified ?mode "?" ?variables ?body))) (body))
    (rule special-lambda (head (metta-equation
      (tptp-print-v1:special
        ?mode λ (LCons ?variables (LCons ?body LNil)))
      (tptp-print-v1:quantified ?mode "^" ?variables ?body))) (body))
    (rule special-pi (head (metta-equation
      (tptp-print-v1:special
        ?mode Π (LCons ?variables (LCons ?body LNil)))
      (tptp-print-v1:quantified ?mode "!>" ?variables ?body))) (body))
    (rule special-sigma (head (metta-equation
      (tptp-print-v1:special
        ?mode Σ (LCons ?variables (LCons ?body LNil)))
      (tptp-print-v1:quantified ?mode "?*" ?variables ?body))) (body))
    (rule special-choice (head (metta-equation
      (tptp-print-v1:special
        ?mode choice (LCons ?variables (LCons ?body LNil)))
      (tptp-print-v1:quantified ?mode "@+" ?variables ?body))) (body))
    (rule special-definite (head (metta-equation
      (tptp-print-v1:special
        ?mode definite (LCons ?variables (LCons ?body LNil)))
      (tptp-print-v1:quantified ?mode "@-" ?variables ?body))) (body))
    (rule special-typing (head (metta-equation
      (tptp-print-v1:special
        ?mode : (LCons ?left (LCons ?right LNil)))
      (tptp-print-v1:cat3
        (tptp-print-v1:expr ?mode ?left)
        (bnf-v1:string->text ":")
        (tptp-print-v1:expr TptpPrint:Type ?right)))) (body))
    (rule special-map-type (head (metta-equation
      (tptp-print-v1:special
        ?mode ⇒ (LCons ?left (LCons ?right LNil)))
      (tptp-print-v1:type-binary ">" ?left ?right))) (body))
    (rule special-product-type (head (metta-equation
      (tptp-print-v1:special
        ?mode × (LCons ?left (LCons ?right LNil)))
      (tptp-print-v1:type-binary "*" ?left ?right))) (body))
    (rule special-sum-type (head (metta-equation
      (tptp-print-v1:special
        ?mode ⊎ (LCons ?left (LCons ?right LNil)))
      (tptp-print-v1:type-binary "+" ?left ?right))) (body))
    (rule special-rational (head (metta-equation
      (tptp-print-v1:special
        ?mode ℚ (LCons ?numerator (LCons ?denominator LNil)))
      (tptp-print-v1:cat3
        (tptp-print-v1:atom ?numerator)
        (bnf-v1:string->text "/")
        (tptp-print-v1:atom ?denominator)))) (body))
    (rule special-real (head (metta-equation
      (tptp-print-v1:special
        ?mode ℝ (LCons ?lexeme LNil))
      (tptp-print-v1:raw-string ?lexeme))) (body))
    (rule special-distinct (head (metta-equation
      (tptp-print-v1:special
        ?mode ◊ (LCons ?value LNil))
      (tptp-print-v1:quoted tptp-double "\"" ?value))) (body))
    ; A back-quoted word is ` and an upper word, which needs no escape.
    (rule special-backquoted (head (metta-equation
      (tptp-print-v1:special
        ?mode ⌜ (LCons ?value LNil))
      (bnf-v1:string->text (str:concat "`" ?value)))) (body))
    (rule special-tuple (head (metta-equation
      (tptp-print-v1:special
        ?mode ⟨⟩ (LCons ?items LNil))
      (tptp-print-v1:tuple ?mode ?items))) (body))
    (rule special-let (head (metta-equation
      (tptp-print-v1:special
        ?mode §let
        (LCons ?types (LCons ?definitions (LCons ?body LNil))))
      (tptp-print-v1:cat5
        (bnf-v1:string->text "$let(")
        (tptp-print-v1:let-component : ?mode ?types)
        (bnf-v1:string->text ",")
        (tptp-print-v1:let-component ≔ ?mode ?definitions)
        (tptp-print-v1:cat3
          (bnf-v1:string->text ",")
          (tptp-print-v1:expr ?mode ?body)
          (bnf-v1:string->text ")"))))) (body))
    (rule special-definition (head (metta-equation
      (tptp-print-v1:special
        ?mode ≔ (LCons ?left (LCons ?right LNil)))
      (tptp-print-v1:cat3
        (tptp-print-v1:expr ?mode ?left)
        (bnf-v1:string->text ":=")
        (tptp-print-v1:expr ?mode ?right)))) (body))
    (rule special-inference (head (metta-equation
      (tptp-print-v1:special
        ?mode inference
        (LCons ?rule (LCons ?info (LCons ?parents LNil))))
      (tptp-print-v1:cat5
        (bnf-v1:string->text "inference(")
        (tptp-print-v1:expr TptpPrint:General ?rule)
        (bnf-v1:string->text ",")
        (tptp-print-v1:brackets TptpPrint:General ?info)
        (tptp-print-v1:cat3
          (bnf-v1:string->text ",")
          (tptp-print-v1:brackets TptpPrint:General ?parents)
          (bnf-v1:string->text ")"))))) (body))
    (rule special-file-one (head (metta-equation
      (tptp-print-v1:special
        ?mode file (LCons ?path LNil))
      (tptp-print-v1:cat3
        (bnf-v1:string->text "file(")
        (tptp-print-v1:name ?path)
        (bnf-v1:string->text ")")))) (body))
    (rule special-file-two (head (metta-equation
      (tptp-print-v1:special
        ?mode file (LCons ?path (LCons ?name LNil)))
      (tptp-print-v1:cat5
        (bnf-v1:string->text "file(")
        (tptp-print-v1:name ?path)
        (bnf-v1:string->text ",")
        (tptp-print-v1:name ?name)
        (bnf-v1:string->text ")")))) (body))
    (rule special-introduced-two (head (metta-equation
      (tptp-print-v1:special
        ?mode introduced (LCons ?kind (LCons ?info LNil)))
      (tptp-print-v1:cat5
        (bnf-v1:string->text "introduced(")
        (tptp-print-v1:expr TptpPrint:General ?kind)
        (bnf-v1:string->text ",")
        (tptp-print-v1:brackets TptpPrint:General ?info)
        (bnf-v1:string->text ")")))) (body))
    (rule special-introduced-three (head (metta-equation
      (tptp-print-v1:special
        ?mode introduced
        (LCons ?kind (LCons ?info (LCons ?parents LNil))))
      (tptp-print-v1:cat5
        (bnf-v1:string->text "introduced(")
        (tptp-print-v1:expr TptpPrint:General ?kind)
        (bnf-v1:string->text ",")
        (tptp-print-v1:brackets TptpPrint:General ?info)
        (tptp-print-v1:cat3
          (bnf-v1:string->text ",")
          (tptp-print-v1:brackets TptpPrint:General ?parents)
          (bnf-v1:string->text ")"))))) (body))
    (rule special-creator (head (metta-equation
      (tptp-print-v1:special
        ?mode creator
        (LCons ?name (LCons ?info (LCons ?parents LNil))))
      (tptp-print-v1:cat5
        (bnf-v1:string->text "creator(")
        (tptp-print-v1:expr TptpPrint:General ?name)
        (bnf-v1:string->text ",")
        (tptp-print-v1:brackets TptpPrint:General ?info)
        (tptp-print-v1:cat3
          (bnf-v1:string->text ",")
          (tptp-print-v1:brackets TptpPrint:General ?parents)
          (bnf-v1:string->text ")"))))) (body))
    (rule special-theory-one (head (metta-equation
      (tptp-print-v1:special
        ?mode theory (LCons ?name LNil))
      (tptp-print-v1:cat3
        (bnf-v1:string->text "theory(")
        (tptp-print-v1:expr TptpPrint:General ?name)
        (bnf-v1:string->text ")")))) (body))
    (rule special-theory-two (head (metta-equation
      (tptp-print-v1:special
        ?mode theory (LCons ?name (LCons ?info LNil)))
      (tptp-print-v1:cat5
        (bnf-v1:string->text "theory(")
        (tptp-print-v1:expr TptpPrint:General ?name)
        (bnf-v1:string->text ",")
        (tptp-print-v1:brackets TptpPrint:General ?info)
        (bnf-v1:string->text ")")))) (body))

    (rule raw-string (head (metta-equation
      (tptp-print-v1:raw-string ?value)
      (tptp-print-v1:raw-string-view
        (langdef:value-view ?value)))) (body))
    (rule raw-string-value (head (metta-equation
      (tptp-print-v1:raw-string-view
        (LangDef:StringValue ?string))
      (bnf-v1:string->text ?string))) (body))

    (rule unary (head (metta-equation
      (tptp-print-v1:unary ?mode ?operator ?body)
      (tptp-print-v1:cat2
        (bnf-v1:string->text ?operator)
        (tptp-print-v1:expr ?mode ?body)))) (body))
    (rule binary-two (head (metta-equation
      (tptp-print-v1:binary
        ?mode ?operator ?head
        (LCons ?left (LCons ?right LNil)))
      (tptp-print-v1:cat5
        (bnf-v1:string->text "(")
        (tptp-print-v1:expr ?mode ?left)
        (bnf-v1:string->text ?operator)
        (tptp-print-v1:expr ?mode ?right)
        (bnf-v1:string->text ")")))) (body))
    (rule infix-two (head (metta-equation
      (tptp-print-v1:infix
        ?mode ?operator ?head
        (LCons ?left (LCons ?right LNil)))
      (tptp-print-v1:cat3
        (tptp-print-v1:infix-operand ?mode ?left)
        (bnf-v1:string->text ?operator)
        (tptp-print-v1:infix-operand ?mode ?right)))) (body))
    (rule infix-operand (head (metta-equation
      (tptp-print-v1:infix-operand ?mode ?value)
      (tptp-print-v1:infix-operand-view
        ?mode (langdef:value-view ?value)))) (body))
    (rule infix-operand-expression (head (metta-equation
      (tptp-print-v1:infix-operand-view
        ?mode (LangDef:ExpressionValue (LCons ?head ?arguments)))
      (tptp-print-v1:infix-operand-class
        (tptp-print-v1:head-member
          ?head (tptp-print-v1:grouped-heads))
        ?mode (LangDef:ExpressionValue (LCons ?head ?arguments))))) (body))
    (rule infix-operand-empty-expression (head (metta-equation
      (tptp-print-v1:infix-operand-view
        ?mode (LangDef:ExpressionValue LNil))
      (tptp-print-v1:expr-view ?mode (LangDef:ExpressionValue LNil)))) (body))
    (rule infix-operand-symbol (head (metta-equation
      (tptp-print-v1:infix-operand-view
        ?mode (LangDef:SymbolValue ?spelling))
      (tptp-print-v1:expr-view ?mode (LangDef:SymbolValue ?spelling)))) (body))
    (rule infix-operand-string (head (metta-equation
      (tptp-print-v1:infix-operand-view
        ?mode (LangDef:StringValue ?string))
      (tptp-print-v1:expr-view ?mode (LangDef:StringValue ?string)))) (body))
    (rule infix-operand-integer (head (metta-equation
      (tptp-print-v1:infix-operand-view
        ?mode (LangDef:IntegerValue ?string))
      (tptp-print-v1:expr-view ?mode (LangDef:IntegerValue ?string)))) (body))
    (rule infix-operand-grouped (head (metta-equation
      (tptp-print-v1:infix-operand-class
        TptpPrint:Special ?mode ?view)
      (tptp-print-v1:cat3
        (bnf-v1:string->text "(")
        (tptp-print-v1:expr-view ?mode ?view)
        (bnf-v1:string->text ")")))) (body))
    (rule infix-operand-plain (head (metta-equation
      (tptp-print-v1:infix-operand-class
        TptpPrint:Application ?mode ?view)
      (tptp-print-v1:expr-view ?mode ?view))) (body))
    (rule grouped-heads (head (metta-equation
      (tptp-print-v1:grouped-heads)
      (LCons ¬ (LCons ∧ (LCons ∨ (LCons → (LCons ← (LCons ↔
      (LCons ⊕ (LCons ⊼ (LCons ⊽ (LCons ≈ (LCons ≉ (LCons ≡
      (LCons ∀ (LCons ∃ (LCons λ (LCons Π (LCons Σ
      (LCons choice (LCons definite LNil))))))))))))))))))))) (body))
    (rule binary-assoc-two (head (metta-equation
      (tptp-print-v1:binary-assoc
        ?mode ?operator ?head
        (LCons ?left (LCons ?right LNil)))
      (tptp-print-v1:cat3
        (bnf-v1:string->text "(")
        (tptp-print-v1:binary-chain
          ?mode ?operator ?head
          (LCons ?left (LCons ?right LNil)))
        (bnf-v1:string->text ")")))) (body))
    (rule binary-chain-two (head (metta-equation
      (tptp-print-v1:binary-chain
        ?mode ?operator ?head
        (LCons ?left (LCons ?right LNil)))
      (tptp-print-v1:cat3
        (tptp-print-v1:binary-left ?mode ?operator ?head ?left)
        (bnf-v1:string->text ?operator)
        (tptp-print-v1:expr ?mode ?right)))) (body))
    (rule binary-left (head (metta-equation
      (tptp-print-v1:binary-left ?mode ?operator ?head ?value)
      (tptp-print-v1:binary-left-view
        ?mode ?operator ?head (langdef:value-view ?value)))) (body))
    (rule binary-left-expression (head (metta-equation
      (tptp-print-v1:binary-left-view
        ?mode ?operator ?head
        (LangDef:ExpressionValue (LCons ?child-head ?child-arguments)))
      (tptp-print-v1:binary-left-next
        (langdef:ground-equal ?child-head ?head)
        ?mode ?operator ?head ?child-head ?child-arguments))) (body))
    (rule binary-left-empty-expression (head (metta-equation
      (tptp-print-v1:binary-left-view
        ?mode ?operator ?head (LangDef:ExpressionValue LNil))
      (tptp-print-v1:expr-view ?mode (LangDef:ExpressionValue LNil)))) (body))
    (rule binary-left-symbol (head (metta-equation
      (tptp-print-v1:binary-left-view
        ?mode ?operator ?head (LangDef:SymbolValue ?spelling))
      (tptp-print-v1:expr-view ?mode (LangDef:SymbolValue ?spelling)))) (body))
    (rule binary-left-string (head (metta-equation
      (tptp-print-v1:binary-left-view
        ?mode ?operator ?head (LangDef:StringValue ?string))
      (tptp-print-v1:expr-view ?mode (LangDef:StringValue ?string)))) (body))
    (rule binary-left-integer (head (metta-equation
      (tptp-print-v1:binary-left-view
        ?mode ?operator ?head (LangDef:IntegerValue ?string))
      (tptp-print-v1:expr-view ?mode (LangDef:IntegerValue ?string)))) (body))
    (rule binary-left-same (head (metta-equation
      (tptp-print-v1:binary-left-next
        LangDef:SameValue
        ?mode ?operator ?head ?child-head ?child-arguments)
      (tptp-print-v1:binary-chain
        ?mode ?operator ?head ?child-arguments))) (body))
    (rule binary-left-different (head (metta-equation
      (tptp-print-v1:binary-left-next
        LangDef:DifferentValue
        ?mode ?operator ?head ?child-head ?child-arguments)
      (tptp-print-v1:expr-view
        ?mode (LangDef:ExpressionValue (LCons ?child-head ?child-arguments))))) (body))
    (rule quantified (head (metta-equation
      (tptp-print-v1:quantified ?mode ?operator ?variables ?body)
      (tptp-print-v1:cat5
        (bnf-v1:string->text ?operator)
        (bnf-v1:string->text "[")
        (tptp-print-v1:variables ?variables)
        (bnf-v1:string->text "]:(")
        (tptp-print-v1:cat2
          (tptp-print-v1:expr ?mode ?body)
          (bnf-v1:string->text ")"))))) (body))
    (rule variables (head (metta-equation
      (tptp-print-v1:variables ?variables)
      (tptp-print-v1:variables-view
        (langdef:value-view ?variables)))) (body))
    (rule variables-expression (head (metta-equation
      (tptp-print-v1:variables-view
        (LangDef:ExpressionValue ?items))
      (tptp-print-v1:items TptpPrint:Type ?items))) (body))
    (rule variables-list (head (metta-equation
      (tptp-print-v1:variables-view
        (LangDef:ListValue ?items))
      (tptp-print-v1:items TptpPrint:Type ?items))) (body))
    (rule type-binary (head (metta-equation
      (tptp-print-v1:type-binary ?operator ?left ?right)
      (tptp-print-v1:cat5
        (bnf-v1:string->text "(")
        (tptp-print-v1:expr TptpPrint:Type ?left)
        (bnf-v1:string->text ?operator)
        (tptp-print-v1:expr TptpPrint:Type ?right)
        (bnf-v1:string->text ")")))) (body))

    (rule call (head (metta-equation
      (tptp-print-v1:call ?mode ?head ?arguments)
      (tptp-print-v1:call-next
        (langdef:ground-equal ?mode TptpPrint:TH)
        ?mode ?head ?arguments))) (body))
    (rule call-th (head (metta-equation
      (tptp-print-v1:call-next
        LangDef:SameValue ?mode ?head ?arguments)
      (tptp-print-v1:th-call ?head ?arguments))) (body))
    (rule call-ordinary (head (metta-equation
      (tptp-print-v1:call-next
        LangDef:DifferentValue ?mode ?head ?arguments)
      (tptp-print-v1:ordinary-call ?head ?arguments))) (body))
    (rule ordinary-call-empty (head (metta-equation
      (tptp-print-v1:ordinary-call ?head LNil)
      (tptp-print-v1:atom ?head))) (body))
    (rule ordinary-call-arguments (head (metta-equation
      (tptp-print-v1:ordinary-call
        ?head (LCons ?first ?rest))
      (tptp-print-v1:cat4
        (tptp-print-v1:atom ?head)
        (bnf-v1:string->text "(")
        (tptp-print-v1:items
          TptpPrint:General (LCons ?first ?rest))
        (bnf-v1:string->text ")")))) (body))
    (rule th-call-empty (head (metta-equation
      (tptp-print-v1:th-call ?head LNil)
      (tptp-print-v1:atom ?head))) (body))
    (rule th-call-arguments (head (metta-equation
      (tptp-print-v1:th-call
        ?head (LCons ?first ?rest))
      (tptp-print-v1:cat3
        (bnf-v1:string->text "(")
        (tptp-print-v1:cat2
          (tptp-print-v1:atom ?head)
          (tptp-print-v1:th-arguments
            (LCons ?first ?rest)))
        (bnf-v1:string->text ")")))) (body))
    (rule th-arguments-empty (head (metta-equation
      (tptp-print-v1:th-arguments LNil)
      (bnf-v1:text-nil))) (body))
    (rule th-arguments-cons (head (metta-equation
      (tptp-print-v1:th-arguments
        (LCons ?argument ?rest))
      (tptp-print-v1:cat4
        (bnf-v1:string->text " @ (")
        (tptp-print-v1:expr TptpPrint:TH ?argument)
        (bnf-v1:string->text ")")
        (tptp-print-v1:th-arguments ?rest)))) (body))

    (rule items-empty (head (metta-equation
      (tptp-print-v1:items ?mode LNil)
      (bnf-v1:text-nil))) (body))
    (rule items-one (head (metta-equation
      (tptp-print-v1:items ?mode (LCons ?item LNil))
      (tptp-print-v1:expr ?mode ?item))) (body))
    (rule items-many (head (metta-equation
      (tptp-print-v1:items
        ?mode (LCons ?item (LCons ?next ?rest)))
      (tptp-print-v1:cat3
        (tptp-print-v1:expr ?mode ?item)
        (bnf-v1:string->text ",")
        (tptp-print-v1:items
          ?mode (LCons ?next ?rest))))) (body))
    (rule brackets (head (metta-equation
      (tptp-print-v1:brackets ?mode ?value)
      (tptp-print-v1:brackets-view
        ?mode (langdef:value-view ?value)))) (body))
    (rule brackets-expression (head (metta-equation
      (tptp-print-v1:brackets-view
        ?mode (LangDef:ExpressionValue ?items))
      (tptp-print-v1:cat3
        (bnf-v1:string->text "[")
        (tptp-print-v1:items ?mode ?items)
        (bnf-v1:string->text "]")))) (body))
    (rule brackets-list (head (metta-equation
      (tptp-print-v1:brackets-view
        ?mode (LangDef:ListValue ?items))
      (tptp-print-v1:cat3
        (bnf-v1:string->text "[")
        (tptp-print-v1:items ?mode ?items)
        (bnf-v1:string->text "]")))) (body))
    (rule tuple (head (metta-equation
      (tptp-print-v1:tuple ?mode ?items)
      (tptp-print-v1:brackets ?mode ?items))) (body))

    (rule let-component (head (metta-equation
      (tptp-print-v1:let-component ?marker ?mode ?value)
      (tptp-print-v1:let-component-view
        ?marker ?mode (langdef:value-view ?value)))) (body))
    (rule let-component-expression (head (metta-equation
      (tptp-print-v1:let-component-view
        ?marker ?mode (LangDef:ExpressionValue ?items))
      (tptp-print-v1:let-component-list
        ?marker ?mode ?items))) (body))
    (rule let-component-list-value (head (metta-equation
      (tptp-print-v1:let-component-view
        ?marker ?mode (LangDef:ListValue ?items))
      (tptp-print-v1:brackets-view ?mode (LangDef:ListValue ?items)))) (body))
    (rule let-component-list-empty (head (metta-equation
      (tptp-print-v1:let-component-list
        ?marker ?mode LNil)
      (tptp-print-v1:brackets-view ?mode (LangDef:ExpressionValue LNil)))) (body))
    (rule let-component-list-cons (head (metta-equation
      (tptp-print-v1:let-component-list
        ?marker ?mode (LCons ?head ?rest))
      (tptp-print-v1:let-component-head
        ?marker ?mode ?rest (langdef:value-view ?head)))) (body))
    (rule let-component-symbol-head (head (metta-equation
      (tptp-print-v1:let-component-head
        ?marker ?mode ?rest (LangDef:SymbolValue ?spelling))
      (tptp-print-v1:let-component-choice
        (langdef:ground-equal
          (str:string->symbol ?spelling) ?marker)
        ?mode
        (LangDef:ExpressionValue
          (LCons (str:string->symbol ?spelling) ?rest))))) (body))
    (rule let-component-expression-head (head (metta-equation
      (tptp-print-v1:let-component-head
        ?marker ?mode ?rest (LangDef:ExpressionValue ?items))
      (tptp-print-v1:brackets-view
        ?mode (LangDef:ExpressionValue
                (LCons (langdef:list->expression ?items) ?rest))))) (body))
    (rule let-component-string-head (head (metta-equation
      (tptp-print-v1:let-component-head
        ?marker ?mode ?rest (LangDef:StringValue ?text))
      (tptp-print-v1:brackets-view
        ?mode (LangDef:ExpressionValue (LCons ?text ?rest))))) (body))
    (rule let-component-integer-head (head (metta-equation
      (tptp-print-v1:let-component-head
        ?marker ?mode ?rest (LangDef:IntegerValue ?text))
      (tptp-print-v1:brackets-view
        ?mode (LangDef:ExpressionValue
                (LCons (str:string->number ?text) ?rest))))) (body))
    (rule let-component-single (head (metta-equation
      (tptp-print-v1:let-component-choice
        LangDef:SameValue ?mode ?view)
      (tptp-print-v1:expr-view ?mode ?view))) (body))
    (rule let-component-many (head (metta-equation
      (tptp-print-v1:let-component-choice
        LangDef:DifferentValue ?mode ?view)
      (tptp-print-v1:brackets-view ?mode ?view))) (body))))
