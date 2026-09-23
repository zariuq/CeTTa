; Compact public TPTP records from the official-grammar observation CST.
; Parser helpers, layout and generated EBNF names stay out of this view.
; Spans are associated metadata. Duplicate occurrences remain distinct.
; This is the authored reference projection, not a native specialization.
(gslt-presentation-v1 TptpOfficialSyntaxRecordsV1
  (signature
    (operator metta-equation 2) (operator let 3)
    (operator langdef:ground-equal 2)
    (operator langdef:string->codepoints 1)
    (operator langdef:codepoints->string 1)
    (operator langdef:codepoints->symbol 1)
    (operator langdef:decimal-integer->number 1)
    (operator langdef:decimal-rational->parts 1)
    (operator langdef:list->expression 1)
    (operator langdef:expression-or-single->list 1)
    (operator cp 1) (operator cons 2)
    (operator LNil 0) (operator LCons 2)
    (operator tptp-cst:spanned-node 4) (operator tptp-cst:production 2)
    (operator tptp-cst:token 4) (operator tptp-cst:literal 3)
    (operator tptp-rec-v1:yes 0) (operator tptp-rec-v1:no 0)
    (operator tptp-rec-v1:node 1) (operator tptp-rec-v1:named 5)
    (operator tptp-rec-v1:after-unwrap 6) (operator tptp-rec-v1:unwrap-choice 6)
    (operator tptp-rec-v1:unwrap-lexical 1)
    (operator tptp-rec-v1:is-unwrap 1)
    (operator tptp-rec-v1:unwraps 0) (operator tptp-rec-v1:member 2)
    (operator tptp-rec-v1:member-next 3) (operator tptp-rec-v1:build 5)
    (operator tptp-rec-v1:nodes 1) (operator tptp-rec-v1:one 1)
    (operator tptp-rec-v1:child 2) (operator tptp-rec-v1:child-next 4)
    (operator tptp-rec-v1:inputs 1) (operator tptp-rec-v1:word 1)
    (operator tptp-rec-v1:word-kind 2) (operator tptp-rec-v1:first 1)
    (operator tptp-rec-v1:token-text 1) (operator tptp-rec-v1:op 1)
    (operator tptp-rec-v1:op-in 1) (operator tptp-rec-v1:binary 3)
    (operator tptp-rec-v1:quant 3) (operator tptp-rec-v1:operands 1)
    (operator tptp-rec-v1:terms 1) (operator tptp-rec-v1:term-elem 1)
    (operator tptp-rec-v1:names 1) (operator tptp-rec-v1:name-elem 1)
    (operator tptp-rec-v1:vars 1) (operator tptp-rec-v1:ann 1)
    (operator tptp-rec-v1:optional 1) (operator tptp-rec-v1:optional-name 1)
    (operator tptp-rec-v1:selection 1)
    (operator tptp-rec-v1:include-options 1)
    (operator tptp-rec-v1:include-options-value 2)
    (operator tptp-rec-v1:include-result 3)
    (operator tptp-rec-v1:atom-from-term 1)
    (operator tptp-rec-v1:head 1) (operator tptp-rec-v1:second 1)
    (operator tptp-rec-v1:third 1) (operator tptp-rec-v1:pair 3)
    (operator tptp-rec-v1:unary 2) (operator tptp-rec-v1:quant-of 2)
    (operator tptp-rec-v1:negated 2) (operator tptp-rec-v1:disjunction 1)
    (operator tptp-rec-v1:plain-term 1) (operator tptp-rec-v1:list2 1)
    (operator tptp-rec-v1:unary-form 1)
    (operator tptp-rec-v1:arg-list 1) (operator tptp-rec-v1:name-seq 1)
    (operator tptp-rec-v1:var-seq 1)
    (operator tptp-rec-v1:tuple-terms 1) (operator tptp-rec-v1:tuple-one-if 3)
    (operator tptp-rec-v1:tuple-one-next 4)
    (operator tptp-rec-v1:defined-plain 2) (operator tptp-rec-v1:defined-plain-next 3)
    (operator tptp-rec-v1:defined-plain-nxf 2) (operator tptp-rec-v1:head-name 1)
    (operator tptp-rec:file 2) (operator tptp-rec:inputs-nil 0)
    (operator tptp-rec:inputs-cons 2) (operator tptp-rec:span 2)
    (operator tptp-rec:fof 5) (operator tptp-rec:cnf 5)
    (operator tptp-rec:tff 5) (operator tptp-rec:thf 5)
    (operator tptp-rec:tcf 5) (operator tptp-rec:tpi 5)
    (operator tptp-rec:include 4)
    (operator tptp-rec:name-word 1) (operator tptp-rec:name-integer 1)
    (operator tptp-rec:word-lower 1) (operator tptp-rec:word-quoted 1)
    (operator tptp-rec:word-back 1) (operator tptp-rec:word-defined 1)
    (operator tptp-rec:word-system 1) (operator tptp-rec:word-upper 1)
    (operator tptp-rec:number-integer 1)
    (operator tptp-rec:number-rational 1)
    (operator tptp-rec:number-real 1)
    (operator tptp-rec:distinct-object 1)
    (operator tptp-rec:role 1) (operator tptp-rec:annotation-none 0)
    (operator tptp-rec:annotation 2) (operator tptp-rec:optional-none 0)
    (operator tptp-rec:optional-info 1)
    (operator tptp-rec:atom 2) (operator tptp-rec:equals 2)
    (operator tptp-rec:not-equals 2) (operator tptp-rec:not 1)
    (operator tptp-rec:and 2) (operator tptp-rec:or 2)
    (operator tptp-rec:iff 2) (operator tptp-rec:implies 2)
    (operator tptp-rec:reverse-implies 2) (operator tptp-rec:xor 2)
    (operator tptp-rec:nor 2) (operator tptp-rec:nand 2)
    (operator tptp-rec:forall 2) (operator tptp-rec:exists 2)
    (operator tptp-rec:apply 2) (operator tptp-rec:typing 2)
    (operator tptp-rec:map-type 2) (operator tptp-rec:fun 2)
    (operator tptp-rec:var 1) (operator tptp-rec:terms-nil 0)
    (operator tptp-rec:terms-cons 2) (operator tptp-rec:vars-nil 0)
    (operator tptp-rec:vars-cons 2) (operator tptp-rec:typed-var 2)
    (operator tptp-rec:include-all 0) (operator tptp-rec:include-names 1)
    (operator tptp-rec:include-star 0) (operator tptp-rec:names-nil 0)
    (operator tptp-rec:names-cons 2) (operator tptp-rec:name-none 0)
    (operator tptp-rec:inference 3) (operator tptp-rec:unknown 0)
    (operator tptp-rec:source-unknown 0)
    (operator tptp-rec:file-source 2) (operator tptp-rec:introduced 3)
    (operator tptp-rec:creator 3) (operator tptp-rec:theory 2)
    (operator tptp-rec:let 3) (operator tptp-rec:long-connective 2)
    (operator tptp-rec:xprod 2) (operator tptp-rec:prefix 2)
    (operator tptp-rec:gen-fun 2) (operator tptp-rec:gen-list 1)
    (operator tptp-rec:gen-colon 2) (operator tptp-rec:type-defined 1)
    (operator tptp-rec:pi 2) (operator tptp-rec:sigma 2)
    (operator tptp-rec:lambda 2) (operator tptp-rec:choice 2)
    (operator tptp-rec:definite 2)
    (operator tptp-rec-v1:general-term-of 1)

    ; Direct compact-value entry.  These operators are projector control,
    ; not public TPTP data constructors.
    (operator tptp-v1:node 1) (operator tptp-v1:named 5)
    (operator tptp-v1:is-unwrap 1)
    (operator tptp-v1:is-unwrap-next 2)
    (operator tptp-v1:after-unwrap 6) (operator tptp-v1:unwrap-choice 6)
    (operator tptp-v1:unwrap-lexical 1) (operator tptp-v1:build 5)
    (operator tptp-v1:inputs 1) (operator tptp-v1:word 1)
    (operator tptp-v1:word-kind 2) (operator tptp-v1:terms 1)
    (operator tptp-v1:variables 1) (operator tptp-v1:plain-term 1)
    (operator tptp-v1:variable-seq 1)
    (operator tptp-v1:append-one 2) (operator tptp-v1:append 2)
    (operator tptp-v1:arguments 1) (operator tptp-v1:flat-items 1)
    (operator tptp-v1:nested-items 1)
    (operator tptp-v1:type-seq 1)
    (operator tptp-v1:tuple-value 1)
    (operator tptp-v1:unbracket 1)
    (operator tptp-v1:atom 1)
    (operator tptp-v1:record 5) (operator tptp-v1:record-next 6)
    (operator tptp-v1:binary 3)
    (operator tptp-v1:quantified 3) (operator tptp-v1:quantified-of 2)
    (operator tptp-v1:pair 3)
    (operator tptp-v1:unary-form 1) (operator tptp-v1:call 2)
    (operator tptp-v1:disjunction 1) (operator tptp-v1:negated 2)
    (operator tptp-v1:defined-name 1) (operator tptp-v1:system-name 1)
    (operator tptp-v1:single-quoted-text 1)
    (operator tptp-v1:distinct-object-text 1)
    (operator tptp-v1:back-quoted-text 1)
    (operator tptp-v1:single-quoted-tail 1)
    (operator tptp-v1:distinct-object-tail 1)
    (operator tptp-v1:back-quoted-tail 1)
    (operator tptp-v1:quoted-body 2)
    (operator tptp-v1:quoted-body-step 4)
    (operator tptp-v1:quoted-body-close 4)
    (operator tptp-v1:quoted-body-escaped 2)
    (operator tptp-v1:rational-parts 1)
    (operator tptp-v1:annotation 1) (operator tptp-v1:optional 1)
    (operator tptp-v1:optional-name 1)
    (operator tptp-v1:annotation-parts 2)
    (operator tptp-v1:annotation-parts-next 3)
    (operator tptp-v1:file-source 2)
    (operator tptp-v1:file-source-next 3)
    (operator tptp-v1:theory-source 2)
    (operator tptp-v1:theory-source-next 3)
    (operator tptp-v1:include-options 1)
    (operator tptp-v1:include-options-value 2)
    (operator tptp-v1:include-with-options 2)
    (operator tptp-v1:include-result 3)
    (operator tptp-v1:include-result-selection-next 4)
    (operator tptp-v1:include-result-qualification-next 4)
    ; TXF tuples are the sole compact escape that cannot be represented as a
    ; bare expression without colliding with ordinary function application.
    (operator ⟨⟩ 1)
    (operator ≡ 2)
    (operator tptp-v1:name-seq 1) (operator tptp-v1:general-term-of 1)
    (operator tptp-v1:general-terms 1)
    (operator tptp-v1:bracketed-sequence 1)
    (operator tptp-v1:head-name 1) (operator tptp-v1:defined-plain 2)
    (operator tptp-v1:defined-plain-next 3)
    (operator tptp-v1:defined-plain-nxf 2)
    (operator tptp-v1:none 0)
    (operator fof 3) (operator cnf 3) (operator tff 3)
    (operator thf 3) (operator tcf 3) (operator tpi 3)
    (operator inference 3)
    (operator annotation 2)
    (operator file 1) (operator file 2)
    (operator introduced 3) (operator creator 3)
    (operator theory 1) (operator theory 2) (operator unknown 0)
    (operator long-connective 2)
    (operator : 2) (operator ⇒ 2) (operator × 2)
    (operator ¬ 1) (operator ∧ 2) (operator ∨ 2)
    (operator → 2) (operator ← 2) (operator ↔ 2)
    (operator ⊕ 2) (operator ⊼ 2) (operator ⊽ 2)
    (operator ≈ 2) (operator ≉ 2)
    (operator ∀ 2) (operator ∃ 2) (operator λ 2)
    (operator choice 2) (operator definite 2)
    (operator Π 2) (operator Σ 2)
    (operator §let 3) (operator ≔ 2)
    (operator tptp:quoted-atom 1) (operator tptp:back-quoted 1)
    (operator tptp:defined-word 1) (operator tptp:system-word 1)
    (operator tptp:integer 1) (operator tptp:rational 1)
    (operator tptp:real 1) (operator tptp:distinct-object 1)
    (operator tptp:identical 2)
    (operator ◊ 1) (operator ⌜ 1)
    (operator ℚ 2) (operator ℝ 1))
  (equations)
  (rewrites
    (rule node (head (metta-equation
      (tptp-rec-v1:node (tptp-cst:spanned-node (tptp-cst:production ?name ?alt) ?start ?stop ?kids))
      (tptp-rec-v1:named ?name ?alt ?start ?stop ?kids))) (body))
    (rule named (head (metta-equation
      (tptp-rec-v1:named ?name ?alt ?start ?stop ?kids)
      (tptp-rec-v1:after-unwrap (tptp-rec-v1:member ?name (tptp-rec-v1:unwraps))
        ?name ?alt ?start ?stop ?kids))) (body))
    (rule unwraps (head (metta-equation (tptp-rec-v1:unwraps)
      (LCons "TPTP_input" (LCons "annotated_formula" (LCons "fof_formula"
      (LCons "fof_logic_formula" (LCons "fof_binary_formula" (LCons "fof_binary_assoc"
      (LCons "fof_unit_formula" (LCons "fof_unitary_formula" (LCons "fof_atomic_formula"
      (LCons "fof_defined_atomic_formula" (LCons "fof_term" (LCons "fof_function_term"
      (LCons "fof_defined_term" (LCons "fof_defined_atomic_term" (LCons "constant"
      (LCons "functor" (LCons "defined_constant" (LCons "defined_functor"
      (LCons "system_constant" (LCons "system_functor"
      (LCons "cnf_formula" (LCons "tpi_formula"
      (LCons "tcf_formula" (LCons "tcf_logic_formula" (LCons "tff_formula"
      (LCons "tff_logic_formula" (LCons "tff_top_level_type" (LCons "tff_non_atomic_type"
      (LCons "tff_unitary_type" (LCons "tff_atomic_type" (LCons "untyped_atom"
      (LCons "type_constant" (LCons "type_functor"
      (LCons "thf_formula" (LCons "thf_logic_formula" (LCons "thf_binary_formula"
      (LCons "thf_binary_assoc" (LCons "thf_unit_formula" (LCons "thf_unitary_formula"
      (LCons "thf_atomic_formula" (LCons "thf_quantifier"
      (LCons "tff_quantifier" (LCons "fof_quantifier" (LCons "thf_top_level_type"
      (LCons "file_name" (LCons "include_optionals" (LCons "dag_source"
      (LCons "external_source" (LCons "intro_type"
      (LCons "creator_name" (LCons "theory_name"
      (LCons "useful_info" (LCons "general_data" (LCons "parent_info"
      (LCons "inference_rule" (LCons "ntf_connective_name"
      (LCons "def_or_sys_constant" (LCons "txf_let_types"
      (LCons "txf_let_defns" (LCons "tff_unary_formula"
      (LCons "tff_defined_atomic" (LCons "tff_atomic_formula"
      (LCons "tff_binary_assoc" (LCons "tff_binary_formula"
      (LCons "tff_preunit_formula" (LCons "tff_term"
      (LCons "tff_unit_formula" (LCons "tff_unitary_formula"
      (LCons "tff_unitary_term" (LCons "txf_unitary_formula"
      (LCons "thf_defined_atomic" (LCons "thf_unitary_type"
      (LCons "thf_unitary_term" (LCons "txf_let_LHS"
      (LCons "txf_tuple_type"
      (LCons "tff_monotype" (LCons "type_quantifier" (LCons "thf_preunit_formula"
      (LCons "thf_unary_formula" (LCons "defined_term" LNil))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))
      (body))
    (rule member-empty (head (metta-equation (tptp-rec-v1:member ?name LNil) (tptp-rec-v1:no))) (body))
    (rule member-cons (head (metta-equation
      (tptp-rec-v1:member ?name (LCons ?head ?tail))
      (tptp-rec-v1:member-next (langdef:ground-equal ?name ?head) ?name ?tail))) (body))
    (rule member-yes (head (metta-equation
      (tptp-rec-v1:member-next LangDef:SameValue ?name ?tail) (tptp-rec-v1:yes))) (body))
    (rule member-no (head (metta-equation
      (tptp-rec-v1:member-next LangDef:DifferentValue ?name ?tail)
      (tptp-rec-v1:member ?name ?tail))) (body))
    (rule after-unwrap-yes (head (metta-equation
      (tptp-rec-v1:after-unwrap (tptp-rec-v1:yes) ?name ?alt ?start ?stop ?kids)
      (tptp-rec-v1:unwrap-choice (tptp-rec-v1:nodes ?kids)
        ?name ?alt ?start ?stop ?kids))) (body))
    (rule unwrap-choice-one (head (metta-equation
      (tptp-rec-v1:unwrap-choice (LCons ?only LNil) ?name ?alt ?start ?stop ?kids)
      (tptp-rec-v1:node ?only))) (body))
    (rule unwrap-choice-empty (head (metta-equation
      (tptp-rec-v1:unwrap-choice LNil ?name ?alt ?start ?stop ?kids)
      (tptp-rec-v1:unwrap-lexical ?kids))) (body))
    (rule unwrap-choice-many (head (metta-equation
      (tptp-rec-v1:unwrap-choice (LCons ?a (LCons ?b ?rest)) ?name ?alt ?start ?stop ?kids)
      (tptp-rec-v1:build ?name ?alt ?start ?stop ?kids))) (body))
    (rule after-unwrap-no (head (metta-equation
      (tptp-rec-v1:after-unwrap (tptp-rec-v1:no) ?name ?alt ?start ?stop ?kids)
      (tptp-rec-v1:build ?name ?alt ?start ?stop ?kids))) (body))
    (rule nodes-empty (head (metta-equation (tptp-rec-v1:nodes LNil) LNil)) (body))
    (rule nodes-node (head (metta-equation
      (tptp-rec-v1:nodes (LCons (tptp-cst:spanned-node ?prod ?start ?stop ?kids) ?rest))
      (LCons (tptp-cst:spanned-node ?prod ?start ?stop ?kids) (tptp-rec-v1:nodes ?rest)))) (body))
    (rule nodes-token (head (metta-equation
      (tptp-rec-v1:nodes (LCons (tptp-cst:token ?kind ?start ?stop ?text) ?rest))
      (tptp-rec-v1:nodes ?rest))) (body))
    (rule nodes-literal (head (metta-equation
      (tptp-rec-v1:nodes (LCons (tptp-cst:literal ?start ?stop ?text) ?rest))
      (tptp-rec-v1:nodes ?rest))) (body))
    (rule unwrap-lexical-empty (head (metta-equation
      (tptp-rec-v1:unwrap-lexical LNil) (tptp-rec:annotation-none))) (body))
    (rule unwrap-lexical-token (head (metta-equation
      (tptp-rec-v1:unwrap-lexical
        (LCons (tptp-cst:token ?kind ?start ?stop ?text) ?rest))
      (tptp-rec-v1:word-kind ?kind ?text))) (body))
    (rule unwrap-lexical-literal (head (metta-equation
      (tptp-rec-v1:unwrap-lexical
        (LCons (tptp-cst:literal ?start ?stop ?text) ?rest))
      (tptp-rec-v1:unwrap-lexical ?rest))) (body))
    (rule one (head (metta-equation (tptp-rec-v1:one (LCons ?node LNil)) ?node)) (body))
    (rule child-hit (head (metta-equation
      (tptp-rec-v1:child ?want
        (LCons (tptp-cst:spanned-node (tptp-cst:production ?found ?alt) ?start ?stop ?kids) ?rest))
      (tptp-rec-v1:child-next (langdef:ground-equal ?want ?found)
        (tptp-cst:spanned-node (tptp-cst:production ?found ?alt) ?start ?stop ?kids)
        ?want ?rest))) (body))
    (rule child-yes (head (metta-equation
      (tptp-rec-v1:child-next LangDef:SameValue ?node ?want ?rest) ?node)) (body))
    (rule child-no (head (metta-equation
      (tptp-rec-v1:child-next LangDef:DifferentValue ?node ?want ?rest)
      (tptp-rec-v1:child ?want ?rest))) (body))
    (rule first-token (head (metta-equation
      (tptp-rec-v1:first (LCons (tptp-cst:token ?kind ?start ?stop ?text) ?rest))
      (tptp-cst:token ?kind ?start ?stop ?text))) (body))
    (rule first-node (head (metta-equation
      (tptp-rec-v1:first (LCons (tptp-cst:spanned-node ?prod ?start ?stop ?kids) ?rest))
      (tptp-cst:spanned-node ?prod ?start ?stop ?kids))) (body))
    (rule first-literal (head (metta-equation
      (tptp-rec-v1:first (LCons (tptp-cst:literal ?start ?stop ?text) ?rest))
      (tptp-rec-v1:first ?rest))) (body))
    (rule word-token (head (metta-equation
      (tptp-rec-v1:word (tptp-cst:token ?kind ?start ?stop ?text))
      (tptp-rec-v1:word-kind ?kind ?text))) (body))
    (rule word-node (head (metta-equation
      (tptp-rec-v1:word (tptp-cst:spanned-node ?prod ?start ?stop ?kids))
      (tptp-rec-v1:word (tptp-rec-v1:first ?kids)))) (body))
    (rule word-lower (head (metta-equation
      (tptp-rec-v1:word-kind "lower_word" ?text) (tptp-rec:word-lower ?text))) (body))
    (rule word-quoted (head (metta-equation
      (tptp-rec-v1:word-kind "single_quoted" ?text) (tptp-rec:word-quoted ?text))) (body))
    (rule word-back (head (metta-equation
      (tptp-rec-v1:word-kind "back_quoted" ?text) (tptp-rec:word-back ?text))) (body))
    (rule word-defined (head (metta-equation
      (tptp-rec-v1:word-kind "dollar_word" ?text) (tptp-rec:word-defined ?text))) (body))
    (rule word-system (head (metta-equation
      (tptp-rec-v1:word-kind "dollar_dollar_word" ?text) (tptp-rec:word-system ?text))) (body))
    (rule word-upper (head (metta-equation
      (tptp-rec-v1:word-kind "upper_word" ?text) (tptp-rec:word-upper ?text))) (body))
    (rule word-integer (head (metta-equation
      (tptp-rec-v1:word-kind "integer" ?text)
      (tptp-rec:number-integer ?text))) (body))
    (rule word-rational (head (metta-equation
      (tptp-rec-v1:word-kind "rational" ?text)
      (tptp-rec:number-rational ?text))) (body))
    (rule word-real (head (metta-equation
      (tptp-rec-v1:word-kind "real" ?text)
      (tptp-rec:number-real ?text))) (body))
    (rule word-distinct (head (metta-equation
      (tptp-rec-v1:word-kind "distinct_object" ?text)
      (tptp-rec:distinct-object ?text))) (body))
    (rule token-text-token (head (metta-equation
      (tptp-rec-v1:token-text (LCons (tptp-cst:token ?kind ?start ?stop ?text) ?rest)) ?text)) (body))
    (rule token-text-literal (head (metta-equation
      (tptp-rec-v1:token-text (LCons (tptp-cst:literal ?start ?stop ?text) ?rest)) ?text)) (body))
    (rule token-text-node (head (metta-equation
      (tptp-rec-v1:token-text (LCons (tptp-cst:spanned-node ?prod ?start ?stop ?kids) ?rest))
      (tptp-rec-v1:token-text ?kids))) (body))
    (rule op-token (head (metta-equation
      (tptp-rec-v1:op (tptp-cst:token ?kind ?start ?stop ?text)) ?text)) (body))
    (rule op-literal (head (metta-equation
      (tptp-rec-v1:op (tptp-cst:literal ?start ?stop ?text)) ?text)) (body))
    (rule op-node (head (metta-equation
      (tptp-rec-v1:op (tptp-cst:spanned-node ?prod ?start ?stop ?kids))
      (tptp-rec-v1:op-in ?kids))) (body))
    (rule op-in-token (head (metta-equation
      (tptp-rec-v1:op-in (LCons (tptp-cst:token ?kind ?start ?stop ?text) ?rest)) ?text)) (body))
    (rule op-in-literal (head (metta-equation
      (tptp-rec-v1:op-in (LCons (tptp-cst:literal ?start ?stop ?text) ?rest)) ?text)) (body))
    (rule op-in-node (head (metta-equation
      (tptp-rec-v1:op-in (LCons (tptp-cst:spanned-node ?prod ?start ?stop ?kids) ?rest))
      (tptp-rec-v1:op (tptp-cst:spanned-node ?prod ?start ?stop ?kids)))) (body))
    (rule operands-two (head (metta-equation
      (tptp-rec-v1:operands (LCons ?left (LCons ?right LNil))) (LCons ?left (LCons ?right LNil)))) (body))
    (rule operands-three (head (metta-equation
      (tptp-rec-v1:operands (LCons ?left (LCons ?op (LCons ?right LNil))))
      (LCons ?left (LCons ?right LNil)))) (body))
    (rule binary-or (head (metta-equation (tptp-rec-v1:binary "|" ?left ?right) (tptp-rec:or ?left ?right))) (body))
    (rule binary-and (head (metta-equation (tptp-rec-v1:binary "&" ?left ?right) (tptp-rec:and ?left ?right))) (body))
    (rule binary-iff (head (metta-equation (tptp-rec-v1:binary "<=>" ?left ?right) (tptp-rec:iff ?left ?right))) (body))
    (rule binary-implies (head (metta-equation (tptp-rec-v1:binary "=>" ?left ?right) (tptp-rec:implies ?left ?right))) (body))
    (rule binary-reverse (head (metta-equation (tptp-rec-v1:binary "<=" ?left ?right) (tptp-rec:reverse-implies ?left ?right))) (body))
    (rule binary-xor (head (metta-equation (tptp-rec-v1:binary "<~>" ?left ?right) (tptp-rec:xor ?left ?right))) (body))
    (rule binary-nor (head (metta-equation (tptp-rec-v1:binary "~|" ?left ?right) (tptp-rec:nor ?left ?right))) (body))
    (rule binary-nand (head (metta-equation (tptp-rec-v1:binary "~&" ?left ?right) (tptp-rec:nand ?left ?right))) (body))
    (rule binary-apply (head (metta-equation (tptp-rec-v1:binary "@" ?left ?right) (tptp-rec:apply ?left ?right))) (body))
    (rule binary-eq (head (metta-equation (tptp-rec-v1:binary "=" ?left ?right) (tptp-rec:equals ?left ?right))) (body))
    (rule binary-identical (head (metta-equation (tptp-rec-v1:binary "==" ?left ?right) (tptp-rec:equals ?left ?right))) (body))
    (rule binary-neq (head (metta-equation (tptp-rec-v1:binary "!=" ?left ?right) (tptp-rec:not-equals ?left ?right))) (body))
    (rule quant-forall (head (metta-equation (tptp-rec-v1:quant "!" ?vars ?body) (tptp-rec:forall ?vars ?body))) (body))
    (rule quant-exists (head (metta-equation (tptp-rec-v1:quant "?" ?vars ?body) (tptp-rec:exists ?vars ?body))) (body))
    (rule quant-lambda (head (metta-equation (tptp-rec-v1:quant "^" ?vars ?body) (tptp-rec:lambda ?vars ?body))) (body))
    (rule quant-choice (head (metta-equation (tptp-rec-v1:quant "@+" ?vars ?body) (tptp-rec:choice ?vars ?body))) (body))
    (rule quant-definite (head (metta-equation (tptp-rec-v1:quant "@-" ?vars ?body) (tptp-rec:definite ?vars ?body))) (body))
    (rule quant-pi (head (metta-equation (tptp-rec-v1:quant "!>" ?vars ?body) (tptp-rec:pi ?vars ?body))) (body))
    (rule quant-sigma (head (metta-equation (tptp-rec-v1:quant "?*" ?vars ?body) (tptp-rec:sigma ?vars ?body))) (body))
    (rule inputs-empty (head (metta-equation (tptp-rec-v1:inputs LNil) (tptp-rec:inputs-nil))) (body))
    (rule inputs-cons (head (metta-equation
      (tptp-rec-v1:inputs (LCons ?head ?rest))
      (tptp-rec:inputs-cons (tptp-rec-v1:node ?head) (tptp-rec-v1:inputs ?rest)))) (body))
    (rule terms-empty (head (metta-equation (tptp-rec-v1:terms LNil) (tptp-rec:terms-nil))) (body))
    (rule terms-cons (head (metta-equation
      (tptp-rec-v1:terms (LCons ?head ?rest))
      (tptp-rec:terms-cons (tptp-rec-v1:term-elem ?head) (tptp-rec-v1:terms ?rest)))) (body))
    (rule term-elem (head (metta-equation (tptp-rec-v1:term-elem ?node) (tptp-rec-v1:node ?node))) (body))
    (rule names-empty (head (metta-equation (tptp-rec-v1:names LNil) (tptp-rec:names-nil))) (body))
    (rule names-cons (head (metta-equation
      (tptp-rec-v1:names (LCons ?head ?rest))
      (tptp-rec:names-cons (tptp-rec-v1:name-elem ?head) (tptp-rec-v1:names ?rest)))) (body))
    (rule name-elem (head (metta-equation (tptp-rec-v1:name-elem ?node) (tptp-rec-v1:node ?node))) (body))
    (rule vars-empty (head (metta-equation (tptp-rec-v1:vars LNil) (tptp-rec:vars-nil))) (body))
    (rule vars-cons (head (metta-equation
      (tptp-rec-v1:vars (LCons ?head ?rest))
      (tptp-rec:vars-cons (tptp-rec-v1:node ?head) (tptp-rec-v1:vars ?rest)))) (body))
    (rule atom-fun (head (metta-equation
      (tptp-rec-v1:atom-from-term (tptp-rec:fun ?symbol ?args))
      (tptp-rec:atom ?symbol ?args))) (body))
    (rule atom-var (head (metta-equation
      (tptp-rec-v1:atom-from-term (tptp-rec:var ?name))
      (tptp-rec:atom ?name (tptp-rec:terms-nil)))) (body))
    (rule atom-word-lower (head (metta-equation
      (tptp-rec-v1:atom-from-term (tptp-rec:word-lower ?text))
      (tptp-rec:atom (tptp-rec:word-lower ?text) (tptp-rec:terms-nil)))) (body))
    (rule atom-word-quoted (head (metta-equation
      (tptp-rec-v1:atom-from-term (tptp-rec:word-quoted ?text))
      (tptp-rec:atom (tptp-rec:word-quoted ?text) (tptp-rec:terms-nil)))) (body))
    (rule atom-word-defined (head (metta-equation
      (tptp-rec-v1:atom-from-term (tptp-rec:word-defined ?text))
      (tptp-rec:atom (tptp-rec:word-defined ?text) (tptp-rec:terms-nil)))) (body))
    (rule build-file (head (metta-equation
      (tptp-rec-v1:build "TPTP_file" ?alt ?start ?stop ?kids)
      (tptp-rec:file (tptp-rec-v1:inputs (tptp-rec-v1:nodes ?kids)) (tptp-rec:span ?start ?stop)))) (body))
    (rule build-fof (head (metta-equation
      (tptp-rec-v1:build "fof_annotated" ?alt ?start ?stop ?kids)
      (tptp-rec:fof
        (tptp-rec-v1:node (tptp-rec-v1:child "name" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:node (tptp-rec-v1:child "formula_role" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:node (tptp-rec-v1:child "fof_formula" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:node (tptp-rec-v1:child "annotations" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec:span ?start ?stop)))) (body))
    (rule build-cnf (head (metta-equation
      (tptp-rec-v1:build "cnf_annotated" ?alt ?start ?stop ?kids)
      (tptp-rec:cnf
        (tptp-rec-v1:node (tptp-rec-v1:child "name" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:node (tptp-rec-v1:child "formula_role" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:node (tptp-rec-v1:child "cnf_formula" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:node (tptp-rec-v1:child "annotations" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec:span ?start ?stop)))) (body))
    (rule build-tff (head (metta-equation
      (tptp-rec-v1:build "tff_annotated" ?alt ?start ?stop ?kids)
      (tptp-rec:tff
        (tptp-rec-v1:node (tptp-rec-v1:child "name" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:node (tptp-rec-v1:child "formula_role" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:node (tptp-rec-v1:child "tff_formula" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:node (tptp-rec-v1:child "annotations" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec:span ?start ?stop)))) (body))
    (rule build-thf (head (metta-equation
      (tptp-rec-v1:build "thf_annotated" ?alt ?start ?stop ?kids)
      (tptp-rec:thf
        (tptp-rec-v1:node (tptp-rec-v1:child "name" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:node (tptp-rec-v1:child "formula_role" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:node (tptp-rec-v1:child "thf_formula" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:node (tptp-rec-v1:child "annotations" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec:span ?start ?stop)))) (body))
    (rule build-tcf (head (metta-equation
      (tptp-rec-v1:build "tcf_annotated" ?alt ?start ?stop ?kids)
      (tptp-rec:tcf
        (tptp-rec-v1:node (tptp-rec-v1:child "name" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:node (tptp-rec-v1:child "formula_role" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:node (tptp-rec-v1:child "tcf_formula" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:node (tptp-rec-v1:child "annotations" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec:span ?start ?stop)))) (body))
    (rule build-tpi (head (metta-equation
      (tptp-rec-v1:build "tpi_annotated" ?alt ?start ?stop ?kids)
      (tptp-rec:tpi
        (tptp-rec-v1:node (tptp-rec-v1:child "name" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:node (tptp-rec-v1:child "formula_role" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:node (tptp-rec-v1:child "tpi_formula" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:node (tptp-rec-v1:child "annotations" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec:span ?start ?stop)))) (body))
    (rule build-include (head (metta-equation
      (tptp-rec-v1:build "include" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:include-result
        (tptp-rec-v1:node (tptp-rec-v1:child "file_name" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:include-options (tptp-rec-v1:nodes ?kids))
        (tptp-rec:span ?start ?stop)))) (body))
    (rule include-result (head (metta-equation
      (tptp-rec-v1:include-result ?file
        (tptp-rec-v1:include-options-value ?selection ?qualification) ?span)
      (tptp-rec:include ?file ?selection ?qualification ?span))) (body))
    (rule include-options-empty (head (metta-equation
      (tptp-rec-v1:include-options LNil)
      (tptp-rec-v1:include-options-value
        (tptp-rec:include-all) (tptp-rec:name-none)))) (body))
    (rule include-options-file (head (metta-equation
      (tptp-rec-v1:include-options
        (LCons (tptp-cst:spanned-node
          (tptp-cst:production "file_name" ?alt) ?s ?e ?kids) ?rest))
      (tptp-rec-v1:include-options ?rest))) (body))
    (rule include-options-wrapper (head (metta-equation
      (tptp-rec-v1:include-options
        (LCons (tptp-cst:spanned-node
          (tptp-cst:production "include_optionals" ?alt) ?s ?e ?kids) ?rest))
      (tptp-rec-v1:include-options (tptp-rec-v1:nodes ?kids)))) (body))
    (rule include-options-nothing (head (metta-equation
      (tptp-rec-v1:include-options
        (LCons (tptp-cst:spanned-node
          (tptp-cst:production "nothing" ?alt) ?s ?e ?kids) ?rest))
      (tptp-rec-v1:include-options-value
        (tptp-rec:include-all) (tptp-rec:name-none)))) (body))
    (rule include-options-selection-only (head (metta-equation
      (tptp-rec-v1:include-options
        (LCons (tptp-cst:spanned-node
          (tptp-cst:production "formula_selection" ?alt) ?s ?e ?kids) LNil))
      (tptp-rec-v1:include-options-value
        (tptp-rec-v1:node
          (tptp-cst:spanned-node
            (tptp-cst:production "formula_selection" ?alt) ?s ?e ?kids))
        (tptp-rec:name-none)))) (body))
    (rule include-options-qualified (head (metta-equation
      (tptp-rec-v1:include-options
        (LCons (tptp-cst:spanned-node
          (tptp-cst:production "formula_selection" ?fa) ?fs ?fe ?fkids)
          (LCons (tptp-cst:spanned-node
            (tptp-cst:production "space_name" ?sa) ?ss ?se ?skids) LNil)))
      (tptp-rec-v1:include-options-value
        (tptp-rec-v1:node
          (tptp-cst:spanned-node
            (tptp-cst:production "formula_selection" ?fa) ?fs ?fe ?fkids))
        (tptp-rec-v1:node
          (tptp-cst:spanned-node
            (tptp-cst:production "space_name" ?sa) ?ss ?se ?skids))))) (body))
    (rule selection-none (head (metta-equation
      (tptp-rec-v1:selection LNil) (tptp-rec:include-all))) (body))
    (rule selection-file (head (metta-equation
      (tptp-rec-v1:selection (LCons (tptp-cst:spanned-node (tptp-cst:production "file_name" ?alt) ?s ?e ?k) ?rest))
      (tptp-rec-v1:selection ?rest))) (body))
    (rule selection-optionals (head (metta-equation
      (tptp-rec-v1:selection (LCons (tptp-cst:spanned-node (tptp-cst:production "include_optionals" ?alt) ?s ?e ?k) ?rest))
      (tptp-rec-v1:selection (tptp-rec-v1:nodes ?k)))) (body))
    (rule selection-nothing (head (metta-equation
      (tptp-rec-v1:selection (LCons (tptp-cst:spanned-node (tptp-cst:production "nothing" ?alt) ?s ?e ?k) ?rest))
      (tptp-rec:include-all))) (body))
    (rule selection-names (head (metta-equation
      (tptp-rec-v1:selection (LCons (tptp-cst:spanned-node (tptp-cst:production "formula_selection" ?alt) ?s ?e ?k) ?rest))
      (tptp-rec-v1:node (tptp-cst:spanned-node (tptp-cst:production "formula_selection" ?alt) ?s ?e ?k)))) (body))
    (rule build-formula-selection-names (head (metta-equation
      (tptp-rec-v1:build "formula_selection" ?alt ?start ?stop ?kids)
      (tptp-rec:include-names (tptp-rec-v1:node (tptp-rec-v1:one (tptp-rec-v1:nodes ?kids)))))) (body))
    (rule build-name-list (head (metta-equation
      (tptp-rec-v1:build "name_list" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:name-seq (tptp-rec-v1:nodes ?kids)))) (body))
    (rule build-name-atomic (head (metta-equation
      (tptp-rec-v1:build "name" 0 ?start ?stop ?kids)
      (tptp-rec:name-word (tptp-rec-v1:word (tptp-rec-v1:first ?kids))))) (body))
    (rule build-name-integer (head (metta-equation
      (tptp-rec-v1:build "name" 1 ?start ?stop ?kids)
      (tptp-rec:name-integer (tptp-rec-v1:token-text ?kids)))) (body))
    (rule build-space-name (head (metta-equation
      (tptp-rec-v1:build "space_name" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:node (tptp-rec-v1:one (tptp-rec-v1:nodes ?kids))))) (body))
    (rule build-atomic-word (head (metta-equation
      (tptp-rec-v1:build "atomic_word" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:word (tptp-rec-v1:first ?kids)))) (body))
    (rule build-atomic-defined-word (head (metta-equation
      (tptp-rec-v1:build "atomic_defined_word" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:word (tptp-rec-v1:first ?kids)))) (body))
    (rule build-atomic-system-word (head (metta-equation
      (tptp-rec-v1:build "atomic_system_word" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:word (tptp-rec-v1:first ?kids)))) (body))
    (rule build-role (head (metta-equation
      (tptp-rec-v1:build "formula_role" ?alt ?start ?stop ?kids)
      (tptp-rec:role (tptp-rec-v1:token-text ?kids)))) (body))
    (rule build-variable (head (metta-equation
      (tptp-rec-v1:build "variable" ?alt ?start ?stop ?kids)
      (tptp-rec:var (tptp-rec-v1:word (tptp-rec-v1:first ?kids))))) (body))
    (rule build-nothing (head (metta-equation
      (tptp-rec-v1:build "nothing" ?alt ?start ?stop ?kids)
      (tptp-rec:annotation-none))) (body))
    (rule build-annotations (head (metta-equation
      (tptp-rec-v1:build "annotations" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:ann (tptp-rec-v1:nodes ?kids)))) (body))
    (rule ann-none (head (metta-equation
      (tptp-rec-v1:ann (LCons (tptp-cst:spanned-node (tptp-cst:production "nothing" ?alt) ?s ?e ?k) LNil))
      (tptp-rec:annotation-none))) (body))
    (rule ann-source (head (metta-equation
      (tptp-rec-v1:ann (LCons ?source (LCons ?optional LNil)))
      (tptp-rec:annotation (tptp-rec-v1:node ?source) (tptp-rec-v1:node ?optional)))) (body))
    (rule build-optional (head (metta-equation
      (tptp-rec-v1:build "optional_info" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:optional (tptp-rec-v1:nodes ?kids)))) (body))
    (rule optional-none (head (metta-equation
      (tptp-rec-v1:optional (LCons (tptp-cst:spanned-node (tptp-cst:production "nothing" ?alt) ?s ?e ?k) LNil))
      (tptp-rec:optional-none))) (body))
    (rule optional-info (head (metta-equation
      (tptp-rec-v1:optional (LCons (tptp-cst:spanned-node (tptp-cst:production "useful_info" ?alt) ?s ?e ?k) LNil))
      (tptp-rec:optional-info
        (tptp-rec-v1:node (tptp-cst:spanned-node (tptp-cst:production "useful_info" ?alt) ?s ?e ?k))))) (body))
    (rule build-source (head (metta-equation
      (tptp-rec-v1:build "source" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:node (tptp-rec-v1:one (tptp-rec-v1:nodes ?kids))))) (body))
    (rule build-inference (head (metta-equation
      (tptp-rec-v1:build "inference_record" ?alt ?start ?stop ?kids)
      (tptp-rec:inference
        (tptp-rec-v1:node (tptp-rec-v1:child "inference_rule" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:node (tptp-rec-v1:child "useful_info" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:node (tptp-rec-v1:child "parents" (tptp-rec-v1:nodes ?kids)))))) (body))
    (rule build-file-source (head (metta-equation
      (tptp-rec-v1:build "file_source" ?alt ?start ?stop ?kids)
      (tptp-rec:file-source
        (tptp-rec-v1:node (tptp-rec-v1:child "file_name" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:node (tptp-rec-v1:child "file_info" (tptp-rec-v1:nodes ?kids)))))) (body))
    (rule build-file-info (head (metta-equation
      (tptp-rec-v1:build "file_info" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:optional-name (tptp-rec-v1:nodes ?kids)))) (body))
    (rule optional-name-none (head (metta-equation
      (tptp-rec-v1:optional-name LNil)
      (tptp-rec:name-none))) (body))
    (rule optional-name-one (head (metta-equation
      (tptp-rec-v1:optional-name (LCons ?name ?rest))
      (tptp-rec-v1:node ?name))) (body))
    (rule build-internal-source (head (metta-equation
      (tptp-rec-v1:build "internal_source" ?alt ?start ?stop ?kids)
      (tptp-rec:introduced
        (tptp-rec-v1:node (tptp-rec-v1:child "intro_type" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:node (tptp-rec-v1:child "useful_info" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:node (tptp-rec-v1:child "parents" (tptp-rec-v1:nodes ?kids)))))) (body))
    (rule build-creator-source (head (metta-equation
      (tptp-rec-v1:build "creator_source" ?alt ?start ?stop ?kids)
      (tptp-rec:creator
        (tptp-rec-v1:node (tptp-rec-v1:child "creator_name" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:node (tptp-rec-v1:child "useful_info" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:node (tptp-rec-v1:child "parents" (tptp-rec-v1:nodes ?kids)))))) (body))
    (rule build-theory (head (metta-equation
      (tptp-rec-v1:build "theory" ?alt ?start ?stop ?kids)
      (tptp-rec:theory
        (tptp-rec-v1:node (tptp-rec-v1:child "theory_name" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:optional (tptp-rec-v1:nodes ?kids))))) (body))
    (rule build-unknown (head (metta-equation
      (tptp-rec-v1:build "unknown" ?alt ?start ?stop ?kids)
      (tptp-rec:source-unknown))) (body))
    (rule build-parents (head (metta-equation
      (tptp-rec-v1:build "parents" ?alt ?start ?stop ?kids)
      (tptp-rec:gen-list (tptp-rec-v1:tuple-terms (tptp-rec-v1:nodes ?kids))))) (body))
    (rule build-parent-list (head (metta-equation
      (tptp-rec-v1:build "parent_list" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:tuple-terms (tptp-rec-v1:nodes ?kids)))) (body))
    (rule build-comma-parent (head (metta-equation
      (tptp-rec-v1:build "comma_parent_info" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:node (tptp-rec-v1:one (tptp-rec-v1:nodes ?kids))))) (body))
    (rule build-parent-details (head (metta-equation
      (tptp-rec-v1:build "parent_details" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:optional (tptp-rec-v1:nodes ?kids)))) (body))
    (rule build-parent-info (head (metta-equation
      (tptp-rec-v1:build "parent_info" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:node (tptp-rec-v1:head (tptp-rec-v1:nodes ?kids))))) (body))
    (rule build-general-term (head (metta-equation
      (tptp-rec-v1:build "general_term" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:general-term-of (tptp-rec-v1:nodes ?kids)))) (body))
    (rule general-term-of-one (head (metta-equation
      (tptp-rec-v1:general-term-of (LCons ?only LNil))
      (tptp-rec-v1:node ?only))) (body))
    (rule general-term-of-colon (head (metta-equation
      (tptp-rec-v1:general-term-of (LCons ?a (LCons ?b ?rest)))
      (tptp-rec:gen-colon (tptp-rec-v1:node ?a) (tptp-rec-v1:node ?b)))) (body))
    (rule build-general-function (head (metta-equation
      (tptp-rec-v1:build "general_function" ?alt ?start ?stop ?kids)
      (tptp-rec:gen-fun
        (tptp-rec-v1:word (tptp-rec-v1:first ?kids))
        (tptp-rec-v1:node (tptp-rec-v1:child "general_terms" (tptp-rec-v1:nodes ?kids)))))) (body))
    (rule build-general-list (head (metta-equation
      (tptp-rec-v1:build "general_list" ?alt ?start ?stop ?kids)
      (tptp-rec:gen-list (tptp-rec-v1:tuple-terms (tptp-rec-v1:nodes ?kids))))) (body))
    (rule build-general-terms (head (metta-equation
      (tptp-rec-v1:build "general_terms" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:tuple-terms (tptp-rec-v1:nodes ?kids)))) (body))
    (rule build-comma-general (head (metta-equation
      (tptp-rec-v1:build "comma_general_term" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:node (tptp-rec-v1:one (tptp-rec-v1:nodes ?kids))))) (body))
    (rule head (head (metta-equation
      (tptp-rec-v1:head (LCons ?first ?rest)) ?first)) (body))
    (rule second (head (metta-equation
      (tptp-rec-v1:second (LCons ?first (LCons ?next ?rest))) ?next)) (body))
    (rule third (head (metta-equation
      (tptp-rec-v1:third (LCons ?first (LCons ?next (LCons ?last ?rest)))) ?last)) (body))
    (rule pair-or (head (metta-equation
      (tptp-rec-v1:pair "or" ?left ?right)
      (tptp-rec:or (tptp-rec-v1:node ?left) (tptp-rec-v1:node ?right)))) (body))
    (rule pair-and (head (metta-equation
      (tptp-rec-v1:pair "and" ?left ?right)
      (tptp-rec:and (tptp-rec-v1:node ?left) (tptp-rec-v1:node ?right)))) (body))
    (rule pair-apply (head (metta-equation
      (tptp-rec-v1:pair "apply" ?left ?right)
      (tptp-rec:apply (tptp-rec-v1:node ?left) (tptp-rec-v1:node ?right)))) (body))
    (rule pair-map (head (metta-equation
      (tptp-rec-v1:pair "map" ?left ?right)
      (tptp-rec:map-type (tptp-rec-v1:node ?left) (tptp-rec-v1:node ?right)))) (body))
    (rule unary-not (head (metta-equation
      (tptp-rec-v1:unary "~" ?body) (tptp-rec:not (tptp-rec-v1:node ?body)))) (body))
    (rule negated-token (head (metta-equation
      (tptp-rec-v1:negated (LCons (tptp-cst:token ?kind ?start ?stop "~") ?rest) ?body)
      (tptp-rec:not ?body))) (body))
    (rule negated-literal (head (metta-equation
      (tptp-rec-v1:negated (LCons (tptp-cst:literal ?start ?stop "~") ?rest) ?body)
      (tptp-rec:not ?body))) (body))
    (rule negated-node (head (metta-equation
      (tptp-rec-v1:negated (LCons (tptp-cst:spanned-node ?prod ?start ?stop ?kids) ?rest) ?body)
      (tptp-rec-v1:negated ?rest ?body))) (body))
    (rule negated-done (head (metta-equation
      (tptp-rec-v1:negated LNil ?body) ?body)) (body))
    (rule disjunction-one (head (metta-equation
      (tptp-rec-v1:disjunction (LCons ?only LNil)) (tptp-rec-v1:node ?only))) (body))
    (rule disjunction-two (head (metta-equation
      (tptp-rec-v1:disjunction (LCons ?left (LCons ?right LNil)))
      (tptp-rec:or (tptp-rec-v1:node ?left) (tptp-rec-v1:node ?right)))) (body))
    (rule arg-list-one (head (metta-equation
      (tptp-rec-v1:arg-list (LCons ?only LNil))
      (tptp-rec:terms-cons (tptp-rec-v1:node ?only) (tptp-rec:terms-nil)))) (body))
    (rule arg-list-two (head (metta-equation
      (tptp-rec-v1:arg-list (LCons ?first (LCons ?rest LNil)))
      (tptp-rec:terms-cons (tptp-rec-v1:node ?first) (tptp-rec-v1:node ?rest)))) (body))
    (rule name-seq-one (head (metta-equation
      (tptp-rec-v1:name-seq (LCons ?only LNil))
      (tptp-rec:names-cons (tptp-rec-v1:node ?only) (tptp-rec:names-nil)))) (body))
    (rule name-seq-two (head (metta-equation
      (tptp-rec-v1:name-seq (LCons ?first (LCons ?rest LNil)))
      (tptp-rec:names-cons (tptp-rec-v1:node ?first) (tptp-rec-v1:node ?rest)))) (body))
    (rule var-seq-one (head (metta-equation
      (tptp-rec-v1:var-seq (LCons ?only LNil))
      (tptp-rec:vars-cons (tptp-rec-v1:node ?only) (tptp-rec:vars-nil)))) (body))
    (rule var-seq-two (head (metta-equation
      (tptp-rec-v1:var-seq (LCons ?first (LCons ?rest LNil)))
      (tptp-rec:vars-cons (tptp-rec-v1:node ?first) (tptp-rec-v1:node ?rest)))) (body))
    (rule plain-term-const (head (metta-equation
      (tptp-rec-v1:plain-term (LCons ?symbol LNil))
      (tptp-rec:fun (tptp-rec-v1:node ?symbol) (tptp-rec:terms-nil)))) (body))
    (rule plain-term-app (head (metta-equation
      (tptp-rec-v1:plain-term (LCons ?symbol (LCons ?args LNil)))
      (tptp-rec:fun (tptp-rec-v1:node ?symbol) (tptp-rec-v1:node ?args)))) (body))
    (rule unary-form-one (head (metta-equation
      (tptp-rec-v1:unary-form (LCons ?only LNil)) (tptp-rec-v1:node ?only))) (body))
    (rule unary-form-two (head (metta-equation
      (tptp-rec-v1:unary-form (LCons ?conn (LCons ?body LNil)))
      (tptp-rec:not (tptp-rec-v1:node ?body)))) (body))
    (rule build-or (head (metta-equation
      (tptp-rec-v1:build "fof_or_formula" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (tptp-rec-v1:pair "or" (tptp-rec-v1:head ?pair) (tptp-rec-v1:second ?pair))))) (body))
    (rule build-and (head (metta-equation
      (tptp-rec-v1:build "fof_and_formula" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (tptp-rec-v1:pair "and" (tptp-rec-v1:head ?pair) (tptp-rec-v1:second ?pair))))) (body))
    (rule build-nonassoc (head (metta-equation
      (tptp-rec-v1:build "fof_binary_nonassoc" ?alt ?start ?stop ?kids)
      (let ?nodes (tptp-rec-v1:nodes ?kids)
        (tptp-rec-v1:binary (tptp-rec-v1:op (tptp-rec-v1:second ?nodes))
          (tptp-rec-v1:node (tptp-rec-v1:head ?nodes))
          (tptp-rec-v1:node (tptp-rec-v1:third ?nodes)))))) (body))
    (rule build-unary (head (metta-equation
      (tptp-rec-v1:build "fof_unary_formula" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:unary-form (tptp-rec-v1:nodes ?kids)))) (body))
    (rule build-plain-atomic (head (metta-equation
      (tptp-rec-v1:build "fof_plain_atomic_formula" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:atom-from-term (tptp-rec-v1:node (tptp-rec-v1:one (tptp-rec-v1:nodes ?kids)))))) (body))
    (rule build-defined-plain-formula (head (metta-equation
      (tptp-rec-v1:build "fof_defined_plain_formula" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:atom-from-term (tptp-rec-v1:node (tptp-rec-v1:one (tptp-rec-v1:nodes ?kids)))))) (body))
    (rule build-infix-unary (head (metta-equation
      (tptp-rec-v1:build "fof_infix_unary" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (tptp-rec:not-equals
          (tptp-rec-v1:node (tptp-rec-v1:head ?pair))
          (tptp-rec-v1:node (tptp-rec-v1:second ?pair)))))) (body))
    (rule build-prefix-unary (head (metta-equation
      (tptp-rec-v1:build "unary_connective" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:token-text ?kids))) (body))
    (rule build-quantified (head (metta-equation
      (tptp-rec-v1:build "fof_quantified_formula" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:quant
        (tptp-rec-v1:op (tptp-rec-v1:child "fof_quantifier" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:node (tptp-rec-v1:child "fof_variable_list" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:node (tptp-rec-v1:child "fof_unit_formula" (tptp-rec-v1:nodes ?kids)))))) (body))
    (rule build-variable-list (head (metta-equation
      (tptp-rec-v1:build "fof_variable_list" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:var-seq (tptp-rec-v1:nodes ?kids)))) (body))
    (rule build-plain-term (head (metta-equation
      (tptp-rec-v1:build "fof_plain_term" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:plain-term (tptp-rec-v1:nodes ?kids)))) (body))
    (rule build-arguments (head (metta-equation
      (tptp-rec-v1:build "fof_arguments" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:arg-list (tptp-rec-v1:nodes ?kids)))) (body))
    (rule build-defined-infix (head (metta-equation
      (tptp-rec-v1:build "fof_defined_infix_formula" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (tptp-rec:equals
          (tptp-rec-v1:node (tptp-rec-v1:head ?pair))
          (tptp-rec-v1:node (tptp-rec-v1:second ?pair)))))) (body))
    (rule build-defined-plain-term (head (metta-equation
      (tptp-rec-v1:build "fof_defined_plain_term" ?alt ?start ?stop ?kids)
      (tptp-rec:fun (tptp-rec-v1:word (tptp-rec-v1:first ?kids)) (tptp-rec:terms-nil)))) (body))
    (rule build-cnf-disjunction (head (metta-equation
      (tptp-rec-v1:build "cnf_disjunction" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:disjunction (tptp-rec-v1:nodes ?kids)))) (body))
    (rule build-cnf-literal (head (metta-equation
      (tptp-rec-v1:build "cnf_literal" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:negated ?kids
        (tptp-rec-v1:node (tptp-rec-v1:one (tptp-rec-v1:nodes ?kids)))))) (body))
    (rule build-tff-typing (head (metta-equation
      (tptp-rec-v1:build "tff_atom_typing" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (tptp-rec:typing
          (tptp-rec-v1:node (tptp-rec-v1:head ?pair))
          (tptp-rec-v1:node (tptp-rec-v1:second ?pair)))))) (body))
    (rule build-mapping (head (metta-equation
      (tptp-rec-v1:build "tff_mapping_type" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (tptp-rec-v1:pair "map" (tptp-rec-v1:head ?pair) (tptp-rec-v1:second ?pair))))) (body))
    (rule build-defined-type (head (metta-equation
      (tptp-rec-v1:build "defined_type" ?alt ?start ?stop ?kids)
      (tptp-rec:type-defined (tptp-rec-v1:word (tptp-rec-v1:first ?kids))))) (body))
    (rule build-thf-apply (head (metta-equation
      (tptp-rec-v1:build "thf_apply_formula" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (tptp-rec-v1:pair "apply" (tptp-rec-v1:head ?pair) (tptp-rec-v1:second ?pair))))) (body))
    (rule build-thf-quantified (head (metta-equation
      (tptp-rec-v1:build "thf_quantified_formula" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:quant-of
        (tptp-rec-v1:node (tptp-rec-v1:child "thf_quantification" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:node (tptp-rec-v1:child "thf_unit_formula" (tptp-rec-v1:nodes ?kids)))))) (body))
    (rule build-thf-quantification (head (metta-equation
      (tptp-rec-v1:build "thf_quantification" ?alt ?start ?stop ?kids)
      (LCons (tptp-rec-v1:op (tptp-rec-v1:child "thf_quantifier" (tptp-rec-v1:nodes ?kids)))
        (LCons (tptp-rec-v1:node (tptp-rec-v1:child "thf_variable_list" (tptp-rec-v1:nodes ?kids))) LNil)))) (body))
    (rule quant-of (head (metta-equation
      (tptp-rec-v1:quant-of (LCons ?op (LCons ?vars LNil)) ?body)
      (tptp-rec-v1:quant ?op ?vars ?body))) (body))
    (rule build-thf-variable-list (head (metta-equation
      (tptp-rec-v1:build "thf_variable_list" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:var-seq (tptp-rec-v1:nodes ?kids)))) (body))
    (rule build-thf-typed-var (head (metta-equation
      (tptp-rec-v1:build "thf_typed_variable" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (tptp-rec:typed-var
          (tptp-rec-v1:node (tptp-rec-v1:head ?pair))
          (tptp-rec-v1:node (tptp-rec-v1:second ?pair)))))) (body))
    (rule build-tcf-quantified (head (metta-equation
      (tptp-rec-v1:build "tcf_quantified_formula" ?alt ?start ?stop ?kids)
      (tptp-rec:forall
        (tptp-rec-v1:node (tptp-rec-v1:child "tff_variable_list" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:node (tptp-rec-v1:child "tcf_logic_formula" (tptp-rec-v1:nodes ?kids)))))) (body))
    (rule build-tff-variable-list (head (metta-equation
      (tptp-rec-v1:build "tff_variable_list" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:var-seq (tptp-rec-v1:nodes ?kids)))) (body))
    (rule build-tff-variable (head (metta-equation
      (tptp-rec-v1:build "tff_variable" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:node (tptp-rec-v1:one (tptp-rec-v1:nodes ?kids))))) (body))
    (rule build-tff-typed-var (head (metta-equation
      (tptp-rec-v1:build "tff_typed_variable" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (tptp-rec:typed-var
          (tptp-rec-v1:node (tptp-rec-v1:head ?pair))
          (tptp-rec-v1:node (tptp-rec-v1:second ?pair)))))) (body))
    (rule build-tff-arguments (head (metta-equation
      (tptp-rec-v1:build "tff_arguments" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:terms (tptp-rec-v1:nodes ?kids)))) (body))
    (rule build-comma-tff (head (metta-equation
      (tptp-rec-v1:build "comma_tff_term" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:node (tptp-rec-v1:one (tptp-rec-v1:nodes ?kids))))) (body))
    (rule build-nonassoc-connective (head (metta-equation
      (tptp-rec-v1:build "nonassoc_connective" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:token-text ?kids))) (body))
    (rule build-defined-infix-pred (head (metta-equation
      (tptp-rec-v1:build "defined_infix_pred" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:token-text ?kids))) (body))
    (rule build-infix-eq (head (metta-equation
      (tptp-rec-v1:build "infix_equality" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:token-text ?kids))) (body))
    (rule build-infix-neq (head (metta-equation
      (tptp-rec-v1:build "infix_inequality" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:token-text ?kids))) (body))
    (rule build-tff-or (head (metta-equation
      (tptp-rec-v1:build "tff_or_formula" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (tptp-rec-v1:pair "or" (tptp-rec-v1:head ?pair) (tptp-rec-v1:second ?pair))))) (body))
    (rule build-tff-and (head (metta-equation
      (tptp-rec-v1:build "tff_and_formula" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (tptp-rec-v1:pair "and" (tptp-rec-v1:head ?pair) (tptp-rec-v1:second ?pair))))) (body))
    (rule build-tff-prefix-unary (head (metta-equation
      (tptp-rec-v1:build "tff_prefix_unary" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:unary-form (tptp-rec-v1:nodes ?kids)))) (body))
    (rule build-tff-infix-unary (head (metta-equation
      (tptp-rec-v1:build "tff_infix_unary" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (tptp-rec:not-equals
          (tptp-rec-v1:node (tptp-rec-v1:head ?pair))
          (tptp-rec-v1:node (tptp-rec-v1:second ?pair)))))) (body))
    (rule build-tff-plain-atomic (head (metta-equation
      (tptp-rec-v1:build "tff_plain_atomic" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:atom-from-term (tptp-rec-v1:plain-term (tptp-rec-v1:nodes ?kids))))) (body))
    (rule build-tff-defined-plain (head (metta-equation
      (tptp-rec-v1:build "tff_defined_plain" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:defined-plain (tptp-rec-v1:head-name (tptp-rec-v1:nodes ?kids))
        (tptp-rec-v1:nodes ?kids)))) (body))
    (rule head-name-prod (head (metta-equation
      (tptp-rec-v1:head-name
        (LCons (tptp-cst:spanned-node (tptp-cst:production ?name ?alt) ?s ?e ?k) ?rest))
      ?name)) (body))
    (rule defined-plain (head (metta-equation
      (tptp-rec-v1:defined-plain ?name ?nodes)
      (tptp-rec-v1:defined-plain-next (langdef:ground-equal ?name "txf_let")
        ?name ?nodes))) (body))
    (rule defined-plain-let (head (metta-equation
      (tptp-rec-v1:defined-plain-next LangDef:SameValue ?name ?nodes)
      (tptp-rec-v1:node (tptp-rec-v1:head ?nodes)))) (body))
    (rule defined-plain-not-let (head (metta-equation
      (tptp-rec-v1:defined-plain-next LangDef:DifferentValue ?name ?nodes)
      (tptp-rec-v1:defined-plain-nxf (langdef:ground-equal ?name "nxf_atom")
        ?nodes))) (body))
    (rule defined-plain-nxf-yes (head (metta-equation
      (tptp-rec-v1:defined-plain-nxf LangDef:SameValue ?nodes)
      (tptp-rec-v1:node (tptp-rec-v1:head ?nodes)))) (body))
    (rule defined-plain-nxf-no (head (metta-equation
      (tptp-rec-v1:defined-plain-nxf LangDef:DifferentValue ?nodes)
      (tptp-rec-v1:atom-from-term (tptp-rec-v1:plain-term ?nodes)))) (body))
    (rule build-txf-let (head (metta-equation
      (tptp-rec-v1:build "txf_let" ?alt ?start ?stop ?kids)
      (let ?nodes (tptp-rec-v1:nodes ?kids)
        (tptp-rec:let
          (tptp-rec-v1:node (tptp-rec-v1:head ?nodes))
          (tptp-rec-v1:node (tptp-rec-v1:second ?nodes))
          (tptp-rec-v1:node (tptp-rec-v1:third ?nodes)))))) (body))
    (rule build-nxf-long (head (metta-equation
      (tptp-rec-v1:build "nxf_long_connective" ?alt ?start ?stop ?kids)
      (tptp-rec:long-connective
        (tptp-rec-v1:node (tptp-rec-v1:one (tptp-rec-v1:nodes ?kids)))
        (tptp-rec:terms-nil)))) (body))
    (rule build-nhf-long (head (metta-equation
      (tptp-rec-v1:build "nhf_long_connective" ?alt ?start ?stop ?kids)
      (tptp-rec:long-connective
        (tptp-rec-v1:node (tptp-rec-v1:one (tptp-rec-v1:nodes ?kids)))
        (tptp-rec:terms-nil)))) (body))
    (rule build-nxf-atom (head (metta-equation
      (tptp-rec-v1:build "nxf_atom" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (tptp-rec-v1:pair "apply" (tptp-rec-v1:head ?pair) (tptp-rec-v1:second ?pair))))) (body))
    (rule build-xprod (head (metta-equation
      (tptp-rec-v1:build "tff_xprod_type" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (tptp-rec:xprod
          (tptp-rec-v1:node (tptp-rec-v1:head ?pair))
          (tptp-rec-v1:node (tptp-rec-v1:second ?pair)))))) (body))
    (rule build-tff-nonassoc (head (metta-equation
      (tptp-rec-v1:build "tff_binary_nonassoc" ?alt ?start ?stop ?kids)
      (let ?nodes (tptp-rec-v1:nodes ?kids)
        (tptp-rec-v1:binary
          (tptp-rec-v1:op (tptp-rec-v1:second ?nodes))
          (tptp-rec-v1:node (tptp-rec-v1:head ?nodes))
          (tptp-rec-v1:node (tptp-rec-v1:third ?nodes)))))) (body))
    (rule build-thf-nonassoc (head (metta-equation
      (tptp-rec-v1:build "thf_binary_nonassoc" ?alt ?start ?stop ?kids)
      (let ?nodes (tptp-rec-v1:nodes ?kids)
        (tptp-rec-v1:binary
          (tptp-rec-v1:op (tptp-rec-v1:second ?nodes))
          (tptp-rec-v1:node (tptp-rec-v1:head ?nodes))
          (tptp-rec-v1:node (tptp-rec-v1:third ?nodes)))))) (body))
    (rule build-tff-quantified (head (metta-equation
      (tptp-rec-v1:build "tff_quantified_formula" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:quant
        (tptp-rec-v1:op (tptp-rec-v1:child "tff_quantifier" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:node (tptp-rec-v1:child "tff_variable_list" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:node (tptp-rec-v1:child "tff_unit_formula" (tptp-rec-v1:nodes ?kids)))))) (body))
    (rule build-tff-type-list (head (metta-equation
      (tptp-rec-v1:build "tff_type_list" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:arg-list (tptp-rec-v1:nodes ?kids)))) (body))
    (rule build-tff-atom-typing-list (head (metta-equation
      (tptp-rec-v1:build "tff_atom_typing_list" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:arg-list (tptp-rec-v1:nodes ?kids)))) (body))
    (rule build-txf-let-defn-list (head (metta-equation
      (tptp-rec-v1:build "txf_let_defn_list" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:arg-list (tptp-rec-v1:nodes ?kids)))) (body))
    (rule build-thf-formula-list (head (metta-equation
      (tptp-rec-v1:build "thf_formula_list" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:terms (tptp-rec-v1:nodes ?kids)))) (body))
    (rule build-comma-thf (head (metta-equation
      (tptp-rec-v1:build "comma_thf_logic_formula" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:node (tptp-rec-v1:one (tptp-rec-v1:nodes ?kids))))) (body))
    (rule build-tff-defined-infix (head (metta-equation
      (tptp-rec-v1:build "tff_defined_infix" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (tptp-rec:equals
          (tptp-rec-v1:node (tptp-rec-v1:head ?pair))
          (tptp-rec-v1:node (tptp-rec-v1:second ?pair)))))) (body))
    (rule build-tff-unary-connective (head (metta-equation
      (tptp-rec-v1:build "tff_unary_connective" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:token-text ?kids))) (body))
    (rule build-thf-atom-typing (head (metta-equation
      (tptp-rec-v1:build "thf_atom_typing" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (tptp-rec:typing
          (tptp-rec-v1:node (tptp-rec-v1:head ?pair))
          (tptp-rec-v1:node (tptp-rec-v1:second ?pair)))))) (body))
    (rule build-thf-plain-atomic (head (metta-equation
      (tptp-rec-v1:build "thf_plain_atomic" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:atom-from-term (tptp-rec-v1:plain-term (tptp-rec-v1:nodes ?kids))))) (body))
    (rule build-thf-and (head (metta-equation
      (tptp-rec-v1:build "thf_and_formula" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (tptp-rec-v1:pair "and" (tptp-rec-v1:head ?pair) (tptp-rec-v1:second ?pair))))) (body))
    (rule build-thf-defined-infix (head (metta-equation
      (tptp-rec-v1:build "thf_defined_infix" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (tptp-rec:equals
          (tptp-rec-v1:node (tptp-rec-v1:head ?pair))
          (tptp-rec-v1:node (tptp-rec-v1:second ?pair)))))) (body))
    (rule build-thf-mapping (head (metta-equation
      (tptp-rec-v1:build "thf_mapping_type" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (tptp-rec-v1:pair "map" (tptp-rec-v1:head ?pair) (tptp-rec-v1:second ?pair))))) (body))
    (rule build-assignment (head (metta-equation
      (tptp-rec-v1:build "assignment" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:token-text ?kids))) (body))
    (rule build-number (head (metta-equation
      (tptp-rec-v1:build "number" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:word (tptp-rec-v1:first ?kids)))) (body))
    (rule build-txf-let-defn (head (metta-equation
      (tptp-rec-v1:build "txf_let_defn" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (tptp-rec:equals
          (tptp-rec-v1:node (tptp-rec-v1:head ?pair))
          (tptp-rec-v1:node (tptp-rec-v1:second ?pair)))))) (body))
    (rule build-txf-tuple (head (metta-equation
      (tptp-rec-v1:build "txf_tuple" ?alt ?start ?stop ?kids)
      (tptp-rec:gen-list (tptp-rec-v1:tuple-terms (tptp-rec-v1:nodes ?kids))))) (body))
    (rule tuple-terms-empty (head (metta-equation
      (tptp-rec-v1:tuple-terms LNil) (tptp-rec:terms-nil))) (body))
    (rule tuple-terms-one (head (metta-equation
      (tptp-rec-v1:tuple-terms
        (LCons (tptp-cst:spanned-node (tptp-cst:production ?name ?alt) ?s ?e ?k) LNil))
      (tptp-rec-v1:tuple-one-if ?name
        (tptp-cst:spanned-node (tptp-cst:production ?name ?alt) ?s ?e ?k)
        (LCons "general_terms" (LCons "parent_list" (LCons "fof_arguments"
        (LCons "tff_arguments" (LCons "thf_formula_list" (LCons "tff_type_list"
        (LCons "tff_atom_typing_list" (LCons "txf_let_defn_list" LNil))))))))))) (body))
    (rule tuple-terms-many (head (metta-equation
      (tptp-rec-v1:tuple-terms (LCons ?a (LCons ?b ?rest)))
      (tptp-rec-v1:terms (LCons ?a (LCons ?b ?rest))))) (body))
    (rule tuple-one-if-empty (head (metta-equation
      (tptp-rec-v1:tuple-one-if ?name ?node LNil)
      (tptp-rec:terms-cons (tptp-rec-v1:node ?node) (tptp-rec:terms-nil)))) (body))
    (rule tuple-one-if-cons (head (metta-equation
      (tptp-rec-v1:tuple-one-if ?name ?node (LCons ?head ?tail))
      (tptp-rec-v1:tuple-one-next (langdef:ground-equal ?name ?head)
        ?name ?node ?tail))) (body))
    (rule tuple-one-next-yes (head (metta-equation
      (tptp-rec-v1:tuple-one-next LangDef:SameValue ?name ?node ?tail)
      (tptp-rec-v1:node ?node))) (body))
    (rule tuple-one-next-no (head (metta-equation
      (tptp-rec-v1:tuple-one-next LangDef:DifferentValue ?name ?node ?tail)
      (tptp-rec-v1:tuple-one-if ?name ?node ?tail))) (body))
    (rule build-tff-atomic-type (head (metta-equation
      (tptp-rec-v1:build "tff_atomic_type" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:plain-term (tptp-rec-v1:nodes ?kids)))) (body))
    (rule build-tf1-quantified (head (metta-equation
      (tptp-rec-v1:build "tf1_quantified_type" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:quant
        (tptp-rec-v1:op (tptp-rec-v1:child "type_quantifier" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:node (tptp-rec-v1:child "tff_variable_list" (tptp-rec-v1:nodes ?kids)))
        (tptp-rec-v1:node (tptp-rec-v1:child "tff_monotype" (tptp-rec-v1:nodes ?kids)))))) (body))
    (rule build-thf-prefix-unary (head (metta-equation
      (tptp-rec-v1:build "thf_prefix_unary" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:unary-form (tptp-rec-v1:nodes ?kids)))) (body))
    (rule build-type-quantifier (head (metta-equation
      (tptp-rec-v1:build "type_quantifier" ?alt ?start ?stop ?kids)
      (tptp-rec-v1:token-text ?kids))) (body))
    (rule build-thf-definition (head (metta-equation
      (tptp-rec-v1:build "thf_definition" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (tptp-rec:equals
          (tptp-rec-v1:node (tptp-rec-v1:head ?pair))
          (tptp-rec-v1:node (tptp-rec-v1:second ?pair)))))) (body))
    (rule build-thf-let-defn (head (metta-equation
      (tptp-rec-v1:build "thf_let_defn" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (tptp-rec:equals
          (tptp-rec-v1:node (tptp-rec-v1:head ?pair))
          (tptp-rec-v1:node (tptp-rec-v1:second ?pair)))))) (body))

    ; Authored compact projection. Grammar-derived production coverage is a
    ; separate gate; this program is not yet a total grammar algebra.
    ; Dynamic source heads are constructed as data by
    ; langdef:list->expression and therefore are not interpreted by the runner.
    (rule compact-node (head (metta-equation
      (tptp-v1:node
        (tptp-cst:spanned-node
          (tptp-cst:production ?name ?alt) ?start ?stop ?kids))
      (tptp-v1:named ?name ?alt ?start ?stop ?kids))) (body))
    (rule compact-named (head (metta-equation
      (tptp-v1:named ?name ?alt ?start ?stop ?kids)
      (tptp-v1:after-unwrap
        (tptp-v1:is-unwrap ?name)
        ?name ?alt ?start ?stop ?kids))) (body))
    (rule compact-is-unwrap (head (metta-equation
      (tptp-v1:is-unwrap ?name)
      (tptp-v1:is-unwrap-next
        (langdef:ground-equal ?name "txf_tuple_type") ?name))) (body))
    (rule compact-is-unwrap-tuple-type (head (metta-equation
      (tptp-v1:is-unwrap-next LangDef:SameValue ?name)
      (tptp-rec-v1:no))) (body))
    (rule compact-is-unwrap-other (head (metta-equation
      (tptp-v1:is-unwrap-next LangDef:DifferentValue ?name)
      (tptp-rec-v1:member ?name (tptp-rec-v1:unwraps)))) (body))
    (rule compact-after-unwrap-yes (head (metta-equation
      (tptp-v1:after-unwrap
        (tptp-rec-v1:yes) ?name ?alt ?start ?stop ?kids)
      (tptp-v1:unwrap-choice
        (tptp-rec-v1:nodes ?kids) ?name ?alt ?start ?stop ?kids))) (body))
    (rule compact-after-unwrap-no (head (metta-equation
      (tptp-v1:after-unwrap
        (tptp-rec-v1:no) ?name ?alt ?start ?stop ?kids)
      (tptp-v1:build ?name ?alt ?start ?stop ?kids))) (body))
    (rule compact-unwrap-one (head (metta-equation
      (tptp-v1:unwrap-choice
        (LCons ?only LNil) ?name ?alt ?start ?stop ?kids)
      (tptp-v1:node ?only))) (body))
    (rule compact-unwrap-empty (head (metta-equation
      (tptp-v1:unwrap-choice LNil ?name ?alt ?start ?stop ?kids)
      (tptp-v1:unwrap-lexical ?kids))) (body))
    (rule compact-unwrap-many (head (metta-equation
      (tptp-v1:unwrap-choice
        (LCons ?a (LCons ?b ?rest)) ?name ?alt ?start ?stop ?kids)
      (tptp-v1:build ?name ?alt ?start ?stop ?kids))) (body))
    (rule compact-unwrap-lexical-empty (head (metta-equation
      (tptp-v1:unwrap-lexical LNil) (tptp-v1:none))) (body))
    (rule compact-unwrap-lexical-token (head (metta-equation
      (tptp-v1:unwrap-lexical
        (LCons (tptp-cst:token ?kind ?start ?stop ?text) ?rest))
      (tptp-v1:word-kind ?kind ?text))) (body))
    (rule compact-unwrap-lexical-literal (head (metta-equation
      (tptp-v1:unwrap-lexical
        (LCons (tptp-cst:literal ?start ?stop ?text) ?rest))
      (tptp-v1:unwrap-lexical ?rest))) (body))

    (rule compact-word-token (head (metta-equation
      (tptp-v1:word (tptp-cst:token ?kind ?start ?stop ?text))
      (tptp-v1:word-kind ?kind ?text))) (body))
    (rule compact-word-node (head (metta-equation
      (tptp-v1:word
        (tptp-cst:spanned-node ?prod ?start ?stop ?kids))
      (tptp-v1:word (tptp-rec-v1:first ?kids)))) (body))
    (rule compact-word-lower (head (metta-equation
      (tptp-v1:word-kind "lower_word" ?text)
      (langdef:codepoints->symbol
        (langdef:string->codepoints ?text)))) (body))
    (rule compact-word-upper (head (metta-equation
      (tptp-v1:word-kind "upper_word" ?text)
      (langdef:codepoints->symbol
        (langdef:string->codepoints ?text)))) (body))
    (rule compact-word-quoted (head (metta-equation
      (tptp-v1:word-kind "single_quoted" ?text)
      (tptp-v1:single-quoted-text ?text))) (body))
    (rule compact-word-back (head (metta-equation
      (tptp-v1:word-kind "back_quoted" ?text)
      (⌜ (tptp-v1:back-quoted-text ?text)))) (body))
    (rule compact-word-defined (head (metta-equation
      (tptp-v1:word-kind "dollar_word" ?text)
      (tptp-v1:defined-name
        (langdef:string->codepoints ?text)))) (body))
    (rule compact-word-system (head (metta-equation
      (tptp-v1:word-kind "dollar_dollar_word" ?text)
      (tptp-v1:system-name
        (langdef:string->codepoints ?text)))) (body))
    (rule compact-defined-name (head (metta-equation
      (tptp-v1:defined-name (cons (cp 36) ?rest))
      (langdef:codepoints->symbol (cons (cp 167) ?rest)))) (body))
    (rule compact-system-name (head (metta-equation
      (tptp-v1:system-name (cons (cp 36) (cons (cp 36) ?rest)))
      (langdef:codepoints->symbol
        (cons (cp 167) (cons (cp 167) ?rest))))) (body))
    (rule compact-single-quoted-text (head (metta-equation
      (tptp-v1:single-quoted-text ?text)
      (langdef:codepoints->string
        (tptp-v1:quoted-body (cp 39)
          (tptp-v1:single-quoted-tail
            (langdef:string->codepoints ?text)))))) (body))
    (rule compact-single-quoted-tail (head (metta-equation
      (tptp-v1:single-quoted-tail (cons (cp 39) ?rest)) ?rest)) (body))
    (rule compact-distinct-object-text (head (metta-equation
      (tptp-v1:distinct-object-text ?text)
      (langdef:codepoints->string
        (tptp-v1:quoted-body (cp 34)
          (tptp-v1:distinct-object-tail
            (langdef:string->codepoints ?text)))))) (body))
    (rule compact-distinct-object-tail (head (metta-equation
      (tptp-v1:distinct-object-tail (cons (cp 34) ?rest)) ?rest)) (body))
    (rule compact-back-quoted-text (head (metta-equation
      (tptp-v1:back-quoted-text ?text)
      (langdef:codepoints->string
        (tptp-v1:back-quoted-tail
          (langdef:string->codepoints ?text))))) (body))
    (rule compact-back-quoted-tail (head (metta-equation
      (tptp-v1:back-quoted-tail (cons (cp 96) ?rest)) ?rest)) (body))
    (rule compact-quoted-body (head (metta-equation
      (tptp-v1:quoted-body ?delimiter (cons ?head ?tail))
      (tptp-v1:quoted-body-step
        (langdef:ground-equal ?head (cp 92)) ?delimiter ?head ?tail))) (body))
    (rule compact-quoted-body-escape (head (metta-equation
      (tptp-v1:quoted-body-step
        LangDef:SameValue ?delimiter ?head ?tail)
      (tptp-v1:quoted-body-escaped ?delimiter ?tail))) (body))
    (rule compact-quoted-body-not-escape (head (metta-equation
      (tptp-v1:quoted-body-step
        LangDef:DifferentValue ?delimiter ?head ?tail)
      (tptp-v1:quoted-body-close
        (langdef:ground-equal ?head ?delimiter) ?delimiter ?head ?tail))) (body))
    (rule compact-quoted-body-done (head (metta-equation
      (tptp-v1:quoted-body-close
        LangDef:SameValue ?delimiter ?head ?tail)
      nil)) (body))
    (rule compact-quoted-body-character (head (metta-equation
      (tptp-v1:quoted-body-close
        LangDef:DifferentValue ?delimiter ?head ?tail)
      (cons ?head (tptp-v1:quoted-body ?delimiter ?tail)))) (body))
    (rule compact-quoted-body-escaped-character (head (metta-equation
      (tptp-v1:quoted-body-escaped ?delimiter (cons ?head ?tail))
      (cons ?head (tptp-v1:quoted-body ?delimiter ?tail)))) (body))
    (rule compact-word-integer (head (metta-equation
      (tptp-v1:word-kind "integer" ?text)
      (langdef:decimal-integer->number ?text))) (body))
    (rule compact-word-rational (head (metta-equation
      (tptp-v1:word-kind "rational" ?text)
      (tptp-v1:rational-parts
        (langdef:decimal-rational->parts ?text)))) (body))
    (rule compact-rational-parts (head (metta-equation
      (tptp-v1:rational-parts (LCons ?numerator (LCons ?denominator LNil)))
      (ℚ ?numerator ?denominator))) (body))
    (rule compact-word-real (head (metta-equation
      (tptp-v1:word-kind "real" ?text)
      (ℝ ?text))) (body))
    (rule compact-word-distinct (head (metta-equation
      (tptp-v1:word-kind "distinct_object" ?text)
      (◊ (tptp-v1:distinct-object-text ?text)))) (body))

    (rule compact-inputs-empty (head (metta-equation
      (tptp-v1:inputs LNil) LNil)) (body))
    (rule compact-inputs-cons (head (metta-equation
      (tptp-v1:inputs (LCons ?head ?rest))
      (LCons (tptp-v1:node ?head) (tptp-v1:inputs ?rest)))) (body))
    (rule compact-terms-empty (head (metta-equation
      (tptp-v1:terms LNil) LNil)) (body))
    (rule compact-terms-cons (head (metta-equation
      (tptp-v1:terms (LCons ?head ?rest))
      (LCons (tptp-v1:node ?head) (tptp-v1:terms ?rest)))) (body))
    (rule compact-vars-empty (head (metta-equation
      (tptp-v1:variables LNil) LNil)) (body))
    (rule compact-vars-cons (head (metta-equation
      (tptp-v1:variables (LCons ?head ?rest))
      (LCons (tptp-v1:node ?head) (tptp-v1:variables ?rest)))) (body))
    (rule compact-variable-seq-one (head (metta-equation
      (tptp-v1:variable-seq (LCons ?only LNil))
      (LCons (tptp-v1:node ?only) LNil))) (body))
    (rule compact-variable-seq-rest (head (metta-equation
      (tptp-v1:variable-seq (LCons ?first (LCons ?rest LNil)))
      (LCons (tptp-v1:node ?first) (tptp-v1:node ?rest)))) (body))
    (rule compact-append-one-empty (head (metta-equation
      (tptp-v1:append-one LNil ?last) (LCons ?last LNil))) (body))
    (rule compact-append-one-cons (head (metta-equation
      (tptp-v1:append-one (LCons ?head ?tail) ?last)
      (LCons ?head (tptp-v1:append-one ?tail ?last)))) (body))
    (rule compact-append-empty (head (metta-equation
      (tptp-v1:append LNil ?right) ?right)) (body))
    (rule compact-append-cons (head (metta-equation
      (tptp-v1:append (LCons ?head ?tail) ?right)
      (LCons ?head (tptp-v1:append ?tail ?right)))) (body))
    (rule compact-call (head (metta-equation
      (tptp-v1:call ?head ?arguments)
      (langdef:list->expression (LCons ?head ?arguments)))) (body))
    (rule compact-plain-const (head (metta-equation
      (tptp-v1:plain-term (LCons ?symbol LNil))
      (tptp-v1:node ?symbol))) (body))
    (rule compact-plain-app (head (metta-equation
      (tptp-v1:plain-term (LCons ?symbol (LCons ?arguments LNil)))
      (tptp-v1:call (tptp-v1:node ?symbol)
        (tptp-v1:node ?arguments)))) (body))
    (rule compact-arguments-one (head (metta-equation
      (tptp-v1:arguments (LCons ?only LNil))
      (LCons (tptp-v1:node ?only) LNil))) (body))
    (rule compact-arguments-rest (head (metta-equation
      (tptp-v1:arguments (LCons ?first (LCons ?rest LNil)))
      (LCons (tptp-v1:node ?first) (tptp-v1:node ?rest)))) (body))
    (rule compact-flat-items-empty (head (metta-equation
      (tptp-v1:flat-items LNil) LNil)) (body))
    (rule compact-flat-items-cons (head (metta-equation
      (tptp-v1:flat-items (LCons ?head ?tail))
      (LCons (tptp-v1:node ?head) (tptp-v1:flat-items ?tail)))) (body))
    (rule compact-nested-items-one (head (metta-equation
      (tptp-v1:nested-items (LCons ?only LNil))
      (LCons (tptp-v1:node ?only) LNil))) (body))
    (rule compact-nested-items-rest (head (metta-equation
      (tptp-v1:nested-items (LCons ?first (LCons ?rest LNil)))
      (LCons
        (tptp-v1:node ?first)
        (langdef:expression-or-single->list
          (tptp-v1:node ?rest))))) (body))
    (rule compact-tuple-value-empty (head (metta-equation
      (tptp-v1:tuple-value LNil)
      (⟨⟩ (langdef:list->expression LNil)))) (body))
    (rule compact-tuple-value-items (head (metta-equation
      (tptp-v1:tuple-value (LCons ?items LNil))
      (⟨⟩
        (langdef:list->expression (tptp-v1:node ?items))))) (body))
    (rule compact-unbracket (head (metta-equation
      (tptp-v1:unbracket (⟨⟩ ?items))
      ?items)) (body))
    (rule compact-atom (head (metta-equation
      (tptp-v1:atom ?term) ?term)) (body))

    (rule compact-binary-or (head (metta-equation
      (tptp-v1:binary "|" ?left ?right) (∨ ?left ?right))) (body))
    (rule compact-binary-and (head (metta-equation
      (tptp-v1:binary "&" ?left ?right) (∧ ?left ?right))) (body))
    (rule compact-binary-iff (head (metta-equation
      (tptp-v1:binary "<=>" ?left ?right) (↔ ?left ?right))) (body))
    (rule compact-binary-implies (head (metta-equation
      (tptp-v1:binary "=>" ?left ?right) (→ ?left ?right))) (body))
    (rule compact-binary-reverse (head (metta-equation
      (tptp-v1:binary "<=" ?left ?right) (← ?left ?right))) (body))
    (rule compact-binary-xor (head (metta-equation
      (tptp-v1:binary "<~>" ?left ?right) (⊕ ?left ?right))) (body))
    (rule compact-binary-nor (head (metta-equation
      (tptp-v1:binary "~|" ?left ?right) (⊽ ?left ?right))) (body))
    (rule compact-binary-nand (head (metta-equation
      (tptp-v1:binary "~&" ?left ?right) (⊼ ?left ?right))) (body))
    (rule compact-binary-eq (head (metta-equation
      (tptp-v1:binary "=" ?left ?right) (≈ ?left ?right))) (body))
    (rule compact-binary-identical (head (metta-equation
      (tptp-v1:binary "==" ?left ?right) (≡ ?left ?right))) (body))
    (rule compact-binary-neq (head (metta-equation
      (tptp-v1:binary "!=" ?left ?right) (≉ ?left ?right))) (body))
    (rule compact-binary-apply (head (metta-equation
      (tptp-v1:binary "@" ?left ?right)
      (langdef:list->expression (LCons ?left (LCons ?right LNil))))) (body))

    (rule compact-quant-forall (head (metta-equation
      (tptp-v1:quantified "!" ?vars ?body)
      (∀ (langdef:list->expression ?vars) ?body))) (body))
    (rule compact-quant-exists (head (metta-equation
      (tptp-v1:quantified "?" ?vars ?body)
      (∃ (langdef:list->expression ?vars) ?body))) (body))
    (rule compact-quant-lambda (head (metta-equation
      (tptp-v1:quantified "^" ?vars ?body)
      (λ (langdef:list->expression ?vars) ?body))) (body))
    (rule compact-quant-choice (head (metta-equation
      (tptp-v1:quantified "@+" ?vars ?body)
      (choice (langdef:list->expression ?vars) ?body))) (body))
    (rule compact-quant-definite (head (metta-equation
      (tptp-v1:quantified "@-" ?vars ?body)
      (definite (langdef:list->expression ?vars) ?body))) (body))
    (rule compact-quant-pi (head (metta-equation
      (tptp-v1:quantified "!>" ?vars ?body)
      (Π (langdef:list->expression ?vars) ?body))) (body))
    (rule compact-quant-sigma (head (metta-equation
      (tptp-v1:quantified "?*" ?vars ?body)
      (Σ (langdef:list->expression ?vars) ?body))) (body))
    (rule compact-quantified-of (head (metta-equation
      (tptp-v1:quantified-of
        (LCons ?operator (LCons ?variables LNil)) ?body)
      (tptp-v1:quantified ?operator ?variables ?body))) (body))
    (rule compact-pair-or (head (metta-equation
      (tptp-v1:pair "or" ?left ?right)
      (∨ (tptp-v1:node ?left) (tptp-v1:node ?right)))) (body))
    (rule compact-pair-and (head (metta-equation
      (tptp-v1:pair "and" ?left ?right)
      (∧ (tptp-v1:node ?left) (tptp-v1:node ?right)))) (body))
    (rule compact-unary-one (head (metta-equation
      (tptp-v1:unary-form (LCons ?only LNil))
      (tptp-v1:node ?only))) (body))
    (rule compact-unary-not (head (metta-equation
      (tptp-v1:unary-form (LCons ?connective (LCons ?body LNil)))
      (¬ (tptp-v1:node ?body)))) (body))
    (rule compact-disjunction-one (head (metta-equation
      (tptp-v1:disjunction (LCons ?only LNil))
      (tptp-v1:node ?only))) (body))
    (rule compact-disjunction-two (head (metta-equation
      (tptp-v1:disjunction (LCons ?left (LCons ?right LNil)))
      (∨ (tptp-v1:node ?left) (tptp-v1:node ?right)))) (body))
    (rule compact-negated-token (head (metta-equation
      (tptp-v1:negated
        (LCons (tptp-cst:token ?kind ?start ?stop "~") ?rest) ?body)
      (¬ ?body))) (body))
    (rule compact-negated-literal (head (metta-equation
      (tptp-v1:negated
        (LCons (tptp-cst:literal ?start ?stop "~") ?rest) ?body)
      (¬ ?body))) (body))
    (rule compact-negated-node (head (metta-equation
      (tptp-v1:negated
        (LCons (tptp-cst:spanned-node ?prod ?start ?stop ?kids) ?rest)
        ?body)
      (tptp-v1:negated ?rest ?body))) (body))
    (rule compact-negated-done (head (metta-equation
      (tptp-v1:negated LNil ?body) ?body)) (body))

    (rule compact-record (head (metta-equation
      (tptp-v1:record ?kind ?name ?role ?formula ?annotation)
      (tptp-v1:record-next
        (langdef:ground-equal ?annotation (tptp-v1:none))
        ?kind ?name ?role ?formula ?annotation))) (body))
    (rule compact-record-no-annotation (head (metta-equation
      (tptp-v1:record-next LangDef:SameValue
        ?kind ?name ?role ?formula ?annotation)
      (langdef:list->expression
        (LCons ?kind (LCons ?name (LCons ?role (LCons ?formula LNil))))))) (body))
    (rule compact-record-annotation (head (metta-equation
      (tptp-v1:record-next LangDef:DifferentValue
        ?kind ?name ?role ?formula ?annotation)
      (langdef:list->expression
        (LCons ?kind
          (LCons ?name
            (LCons ?role
              (LCons ?formula (LCons ?annotation LNil)))))))) (body))
    (rule compact-build-file (head (metta-equation
      (tptp-v1:build "TPTP_file" ?alt ?start ?stop ?kids)
      (langdef:list->expression
        (tptp-v1:inputs (tptp-rec-v1:nodes ?kids))))) (body))
    (rule compact-build-fof (head (metta-equation
      (tptp-v1:build "fof_annotated" ?alt ?start ?stop ?kids)
      (tptp-v1:record fof
        (tptp-v1:node
          (tptp-rec-v1:child "name" (tptp-rec-v1:nodes ?kids)))
        (tptp-v1:node
          (tptp-rec-v1:child "formula_role" (tptp-rec-v1:nodes ?kids)))
        (tptp-v1:node
          (tptp-rec-v1:child "fof_formula" (tptp-rec-v1:nodes ?kids)))
        (tptp-v1:node
          (tptp-rec-v1:child "annotations" (tptp-rec-v1:nodes ?kids)))))) (body))
    (rule compact-build-cnf (head (metta-equation
      (tptp-v1:build "cnf_annotated" ?alt ?start ?stop ?kids)
      (tptp-v1:record cnf
        (tptp-v1:node
          (tptp-rec-v1:child "name" (tptp-rec-v1:nodes ?kids)))
        (tptp-v1:node
          (tptp-rec-v1:child "formula_role" (tptp-rec-v1:nodes ?kids)))
        (tptp-v1:node
          (tptp-rec-v1:child "cnf_formula" (tptp-rec-v1:nodes ?kids)))
        (tptp-v1:node
          (tptp-rec-v1:child "annotations" (tptp-rec-v1:nodes ?kids)))))) (body))
    (rule compact-build-tff (head (metta-equation
      (tptp-v1:build "tff_annotated" ?alt ?start ?stop ?kids)
      (tptp-v1:record tff
        (tptp-v1:node
          (tptp-rec-v1:child "name" (tptp-rec-v1:nodes ?kids)))
        (tptp-v1:node
          (tptp-rec-v1:child "formula_role" (tptp-rec-v1:nodes ?kids)))
        (tptp-v1:node
          (tptp-rec-v1:child "tff_formula" (tptp-rec-v1:nodes ?kids)))
        (tptp-v1:node
          (tptp-rec-v1:child "annotations" (tptp-rec-v1:nodes ?kids)))))) (body))
    (rule compact-build-thf (head (metta-equation
      (tptp-v1:build "thf_annotated" ?alt ?start ?stop ?kids)
      (tptp-v1:record thf
        (tptp-v1:node
          (tptp-rec-v1:child "name" (tptp-rec-v1:nodes ?kids)))
        (tptp-v1:node
          (tptp-rec-v1:child "formula_role" (tptp-rec-v1:nodes ?kids)))
        (tptp-v1:node
          (tptp-rec-v1:child "thf_formula" (tptp-rec-v1:nodes ?kids)))
        (tptp-v1:node
          (tptp-rec-v1:child "annotations" (tptp-rec-v1:nodes ?kids)))))) (body))
    (rule compact-build-tcf (head (metta-equation
      (tptp-v1:build "tcf_annotated" ?alt ?start ?stop ?kids)
      (tptp-v1:record tcf
        (tptp-v1:node
          (tptp-rec-v1:child "name" (tptp-rec-v1:nodes ?kids)))
        (tptp-v1:node
          (tptp-rec-v1:child "formula_role" (tptp-rec-v1:nodes ?kids)))
        (tptp-v1:node
          (tptp-rec-v1:child "tcf_formula" (tptp-rec-v1:nodes ?kids)))
        (tptp-v1:node
          (tptp-rec-v1:child "annotations" (tptp-rec-v1:nodes ?kids)))))) (body))
    (rule compact-build-tpi (head (metta-equation
      (tptp-v1:build "tpi_annotated" ?alt ?start ?stop ?kids)
      (tptp-v1:record tpi
        (tptp-v1:node
          (tptp-rec-v1:child "name" (tptp-rec-v1:nodes ?kids)))
        (tptp-v1:node
          (tptp-rec-v1:child "formula_role" (tptp-rec-v1:nodes ?kids)))
        (tptp-v1:node
          (tptp-rec-v1:child "tpi_formula" (tptp-rec-v1:nodes ?kids)))
        (tptp-v1:node
          (tptp-rec-v1:child "annotations" (tptp-rec-v1:nodes ?kids)))))) (body))
    (rule compact-build-name-atomic (head (metta-equation
      (tptp-v1:build "name" 0 ?start ?stop ?kids)
      (tptp-v1:word (tptp-rec-v1:first ?kids)))) (body))
    (rule compact-build-name-integer (head (metta-equation
      (tptp-v1:build "name" 1 ?start ?stop ?kids)
      (langdef:decimal-integer->number
        (tptp-rec-v1:token-text ?kids)))) (body))
    (rule compact-build-atomic-word (head (metta-equation
      (tptp-v1:build "atomic_word" ?alt ?start ?stop ?kids)
      (tptp-v1:word (tptp-rec-v1:first ?kids)))) (body))
    (rule compact-build-role (head (metta-equation
      (tptp-v1:build "formula_role" ?alt ?start ?stop ?kids)
      (langdef:codepoints->symbol
        (langdef:string->codepoints
          (tptp-rec-v1:token-text ?kids))))) (body))
    (rule compact-build-variable (head (metta-equation
      (tptp-v1:build "variable" ?alt ?start ?stop ?kids)
      (tptp-v1:word (tptp-rec-v1:first ?kids)))) (body))
    (rule compact-build-atomic-defined-word (head (metta-equation
      (tptp-v1:build "atomic_defined_word" ?alt ?start ?stop ?kids)
      (tptp-v1:word (tptp-rec-v1:first ?kids)))) (body))
    (rule compact-build-atomic-system-word (head (metta-equation
      (tptp-v1:build "atomic_system_word" ?alt ?start ?stop ?kids)
      (tptp-v1:word (tptp-rec-v1:first ?kids)))) (body))
    (rule compact-build-nothing (head (metta-equation
      (tptp-v1:build "nothing" ?alt ?start ?stop ?kids)
      (tptp-v1:none))) (body))
    (rule compact-build-annotations (head (metta-equation
      (tptp-v1:build "annotations" ?alt ?start ?stop ?kids)
      (tptp-v1:annotation (tptp-rec-v1:nodes ?kids)))) (body))
    (rule compact-annotation-none (head (metta-equation
      (tptp-v1:annotation
        (LCons (tptp-cst:spanned-node
          (tptp-cst:production "nothing" ?alt) ?start ?stop ?kids) LNil))
      (tptp-v1:none))) (body))
    (rule compact-annotation-source (head (metta-equation
      (tptp-v1:annotation (LCons ?source (LCons ?optional LNil)))
      (tptp-v1:annotation-parts
        (tptp-v1:node ?source) (tptp-v1:node ?optional)))) (body))
    (rule compact-annotation-parts (head (metta-equation
      (tptp-v1:annotation-parts ?source ?info)
      (tptp-v1:annotation-parts-next
        (langdef:ground-equal ?info (tptp-v1:none)) ?source ?info))) (body))
    (rule compact-annotation-parts-none (head (metta-equation
      (tptp-v1:annotation-parts-next LangDef:SameValue ?source ?info)
      ?source)) (body))
    (rule compact-annotation-parts-some (head (metta-equation
      (tptp-v1:annotation-parts-next LangDef:DifferentValue ?source ?info)
      (annotation ?source (tptp-v1:unbracket ?info)))) (body))
    (rule compact-build-optional-info (head (metta-equation
      (tptp-v1:build "optional_info" ?alt ?start ?stop ?kids)
      (tptp-v1:optional (tptp-rec-v1:nodes ?kids)))) (body))
    (rule compact-optional-none (head (metta-equation
      (tptp-v1:optional
        (LCons (tptp-cst:spanned-node
          (tptp-cst:production "nothing" ?alt) ?start ?stop ?kids) LNil))
      (tptp-v1:none))) (body))
    (rule compact-optional-some (head (metta-equation
      (tptp-v1:optional
        (LCons (tptp-cst:spanned-node
          (tptp-cst:production "useful_info" ?alt) ?start ?stop ?kids) LNil))
      (tptp-v1:node
        (tptp-cst:spanned-node
          (tptp-cst:production "useful_info" ?alt) ?start ?stop ?kids)))) (body))
    (rule compact-build-source (head (metta-equation
      (tptp-v1:build "source" ?alt ?start ?stop ?kids)
      (tptp-v1:node (tptp-rec-v1:one (tptp-rec-v1:nodes ?kids))))) (body))
    (rule compact-build-file-source (head (metta-equation
      (tptp-v1:build "file_source" ?alt ?start ?stop ?kids)
      (tptp-v1:file-source
        (tptp-v1:node
          (tptp-rec-v1:child "file_name" (tptp-rec-v1:nodes ?kids)))
        (tptp-v1:node
          (tptp-rec-v1:child "file_info" (tptp-rec-v1:nodes ?kids)))))) (body))
    (rule compact-build-file-info (head (metta-equation
      (tptp-v1:build "file_info" ?alt ?start ?stop ?kids)
      (tptp-v1:optional-name (tptp-rec-v1:nodes ?kids)))) (body))
    (rule compact-optional-name-none (head (metta-equation
      (tptp-v1:optional-name LNil)
      (tptp-v1:none))) (body))
    (rule compact-optional-name-one (head (metta-equation
      (tptp-v1:optional-name (LCons ?name ?rest))
      (tptp-v1:node ?name))) (body))
    (rule compact-file-source (head (metta-equation
      (tptp-v1:file-source ?path ?name)
      (tptp-v1:file-source-next
        (langdef:ground-equal ?name (tptp-v1:none)) ?path ?name))) (body))
    (rule compact-file-source-path (head (metta-equation
      (tptp-v1:file-source-next LangDef:SameValue ?path ?name)
      (file ?path))) (body))
    (rule compact-file-source-name (head (metta-equation
      (tptp-v1:file-source-next LangDef:DifferentValue ?path ?name)
      (file ?path ?name))) (body))
    (rule compact-build-internal-source (head (metta-equation
      (tptp-v1:build "internal_source" ?alt ?start ?stop ?kids)
      (introduced
        (tptp-v1:node
          (tptp-rec-v1:child "intro_type" (tptp-rec-v1:nodes ?kids)))
        (tptp-v1:unbracket
          (tptp-v1:node
            (tptp-rec-v1:child "useful_info" (tptp-rec-v1:nodes ?kids))))
        (tptp-v1:node
          (tptp-rec-v1:child "parents" (tptp-rec-v1:nodes ?kids)))))) (body))
    (rule compact-build-creator-source (head (metta-equation
      (tptp-v1:build "creator_source" ?alt ?start ?stop ?kids)
      (creator
        (tptp-v1:node
          (tptp-rec-v1:child "creator_name" (tptp-rec-v1:nodes ?kids)))
        (tptp-v1:unbracket
          (tptp-v1:node
            (tptp-rec-v1:child "useful_info" (tptp-rec-v1:nodes ?kids))))
        (tptp-v1:node
          (tptp-rec-v1:child "parents" (tptp-rec-v1:nodes ?kids)))))) (body))
    (rule compact-build-theory-source (head (metta-equation
      (tptp-v1:build "theory" ?alt ?start ?stop ?kids)
      (tptp-v1:theory-source
        (tptp-v1:node
          (tptp-rec-v1:child "theory_name" (tptp-rec-v1:nodes ?kids)))
        (tptp-v1:node
          (tptp-rec-v1:child "optional_info" (tptp-rec-v1:nodes ?kids)))))) (body))
    (rule compact-theory-source (head (metta-equation
      (tptp-v1:theory-source ?name ?info)
      (tptp-v1:theory-source-next
        (langdef:ground-equal ?info (tptp-v1:none)) ?name ?info))) (body))
    (rule compact-theory-source-name (head (metta-equation
      (tptp-v1:theory-source-next LangDef:SameValue ?name ?info)
      (theory ?name))) (body))
    (rule compact-theory-source-info (head (metta-equation
      (tptp-v1:theory-source-next LangDef:DifferentValue ?name ?info)
      (theory ?name (tptp-v1:unbracket ?info)))) (body))
    (rule compact-build-unknown-source (head (metta-equation
      (tptp-v1:build "unknown" ?alt ?start ?stop ?kids)
      unknown)) (body))
    (rule compact-build-inference (head (metta-equation
      (tptp-v1:build "inference_record" ?alt ?start ?stop ?kids)
      (inference
        (tptp-v1:node
          (tptp-rec-v1:child "inference_rule" (tptp-rec-v1:nodes ?kids)))
        (tptp-v1:unbracket
          (tptp-v1:node
            (tptp-rec-v1:child "useful_info" (tptp-rec-v1:nodes ?kids))))
        (tptp-v1:node
          (tptp-rec-v1:child "parents" (tptp-rec-v1:nodes ?kids)))))) (body))

    (rule compact-build-include (head (metta-equation
      (tptp-v1:build "include" ?alt ?start ?stop ?kids)
      (tptp-v1:include-options
        (tptp-rec-v1:nodes ?kids)))) (body))
    (rule compact-include-skip-file (head (metta-equation
      (tptp-v1:include-options
        (LCons (tptp-cst:spanned-node
          (tptp-cst:production "file_name" ?alt) ?start ?stop ?file-kids)
          ?rest))
      (tptp-v1:include-with-options
        (tptp-v1:node
          (tptp-cst:spanned-node
            (tptp-cst:production "file_name" ?alt)
            ?start ?stop ?file-kids))
        (tptp-v1:include-options ?rest)))) (body))
    (rule compact-include-optionals-wrapper (head (metta-equation
      (tptp-v1:include-options
        (LCons (tptp-cst:spanned-node
          (tptp-cst:production "include_optionals" ?alt)
          ?start ?stop ?kids) ?rest))
      (tptp-v1:include-options (tptp-rec-v1:nodes ?kids)))) (body))
    (rule compact-include-options-empty (head (metta-equation
      (tptp-v1:include-options LNil)
      (tptp-v1:include-options-value
        (tptp-v1:none) (tptp-v1:none)))) (body))
    (rule compact-include-options-none (head (metta-equation
      (tptp-v1:include-options
        (LCons (tptp-cst:spanned-node
          (tptp-cst:production "nothing" ?alt) ?start ?stop ?kids) ?rest))
      (tptp-v1:include-options-value
        (tptp-v1:none) (tptp-v1:none)))) (body))
    (rule compact-include-options-selection (head (metta-equation
      (tptp-v1:include-options
        (LCons (tptp-cst:spanned-node
          (tptp-cst:production "formula_selection" ?alt)
          ?start ?stop ?kids) LNil))
      (tptp-v1:include-options-value
        (tptp-v1:node
          (tptp-cst:spanned-node
            (tptp-cst:production "formula_selection" ?alt)
            ?start ?stop ?kids))
        (tptp-v1:none)))) (body))
    (rule compact-include-options-qualified (head (metta-equation
      (tptp-v1:include-options
        (LCons (tptp-cst:spanned-node
          (tptp-cst:production "formula_selection" ?fa)
          ?fs ?fe ?fkids)
          (LCons (tptp-cst:spanned-node
            (tptp-cst:production "space_name" ?sa)
            ?ss ?se ?skids) LNil)))
      (tptp-v1:include-options-value
        (tptp-v1:node
          (tptp-cst:spanned-node
            (tptp-cst:production "formula_selection" ?fa)
            ?fs ?fe ?fkids))
        (tptp-v1:node
          (tptp-cst:spanned-node
            (tptp-cst:production "space_name" ?sa)
            ?ss ?se ?skids))))) (body))
    (rule compact-include-with-options (head (metta-equation
      (tptp-v1:include-with-options ?file
        (tptp-v1:include-options-value ?selection ?qualification))
      (tptp-v1:include-result ?file ?selection ?qualification))) (body))
    (rule compact-include-result (head (metta-equation
      (tptp-v1:include-result ?file ?selection ?qualification)
      (tptp-v1:include-result-selection-next
        (langdef:ground-equal ?selection (tptp-v1:none))
        ?file ?selection ?qualification))) (body))
    (rule compact-include-result-all (head (metta-equation
      (tptp-v1:include-result-selection-next LangDef:SameValue
        ?file ?selection ?qualification)
      (langdef:list->expression (LCons incl (LCons ?file LNil))))) (body))
    (rule compact-include-result-some (head (metta-equation
      (tptp-v1:include-result-selection-next LangDef:DifferentValue
        ?file ?selection ?qualification)
      (tptp-v1:include-result-qualification-next
        (langdef:ground-equal ?qualification (tptp-v1:none))
        ?file ?selection ?qualification))) (body))
    (rule compact-include-result-selection (head (metta-equation
      (tptp-v1:include-result-qualification-next LangDef:SameValue
        ?file ?selection ?qualification)
      (langdef:list->expression
        (LCons incl (LCons ?file (LCons ?selection LNil)))))) (body))
    (rule compact-include-result-qualified (head (metta-equation
      (tptp-v1:include-result-qualification-next LangDef:DifferentValue
        ?file ?selection ?qualification)
      (langdef:list->expression
        (LCons incl
          (LCons ?file
            (LCons ?selection (LCons ?qualification LNil))))))) (body))
    (rule compact-build-selection (head (metta-equation
      (tptp-v1:build "formula_selection" ?alt ?start ?stop ?kids)
      (langdef:list->expression
        (tptp-v1:node
          (tptp-rec-v1:one (tptp-rec-v1:nodes ?kids)))))) (body))
    (rule compact-build-name-list (head (metta-equation
      (tptp-v1:build "name_list" ?alt ?start ?stop ?kids)
      (tptp-v1:name-seq (tptp-rec-v1:nodes ?kids)))) (body))
    (rule compact-name-seq-one (head (metta-equation
      (tptp-v1:name-seq (LCons ?only LNil))
      (LCons (tptp-v1:node ?only) LNil))) (body))
    (rule compact-name-seq-rest (head (metta-equation
      (tptp-v1:name-seq (LCons ?first (LCons ?rest LNil)))
      (LCons (tptp-v1:node ?first) (tptp-v1:node ?rest)))) (body))
    (rule compact-build-space-name (head (metta-equation
      (tptp-v1:build "space_name" ?alt ?start ?stop ?kids)
      (tptp-v1:node (tptp-rec-v1:one (tptp-rec-v1:nodes ?kids))))) (body))

    (rule compact-build-general-term (head (metta-equation
      (tptp-v1:build "general_term" ?alt ?start ?stop ?kids)
      (tptp-v1:general-term-of (tptp-rec-v1:nodes ?kids)))) (body))
    (rule compact-general-term-one (head (metta-equation
      (tptp-v1:general-term-of (LCons ?only LNil))
      (tptp-v1:node ?only))) (body))
    (rule compact-general-term-colon (head (metta-equation
      (tptp-v1:general-term-of (LCons ?left (LCons ?right ?rest)))
      (: (tptp-v1:node ?left) (tptp-v1:node ?right)))) (body))
    (rule compact-build-general-function (head (metta-equation
      (tptp-v1:build "general_function" ?alt ?start ?stop ?kids)
      (tptp-v1:call
        (tptp-v1:word (tptp-rec-v1:first ?kids))
        (tptp-v1:node
          (tptp-rec-v1:child "general_terms" (tptp-rec-v1:nodes ?kids)))))) (body))
    (rule compact-build-general-list (head (metta-equation
      (tptp-v1:build "general_list" ?alt ?start ?stop ?kids)
      (⟨⟩
        (tptp-v1:bracketed-sequence
          (tptp-rec-v1:nodes ?kids))))) (body))
    (rule compact-bracketed-sequence-empty (head (metta-equation
      (tptp-v1:bracketed-sequence LNil)
      (langdef:list->expression LNil))) (body))
    (rule compact-bracketed-sequence-one (head (metta-equation
      (tptp-v1:bracketed-sequence (LCons ?items LNil))
      (langdef:list->expression (tptp-v1:node ?items)))) (body))
    (rule compact-build-general-terms (head (metta-equation
      (tptp-v1:build "general_terms" ?alt ?start ?stop ?kids)
      (tptp-v1:general-terms (tptp-rec-v1:nodes ?kids)))) (body))
    (rule compact-general-terms (head (metta-equation
      (tptp-v1:general-terms ?nodes)
      (tptp-v1:terms ?nodes))) (body))
    (rule compact-build-comma-general (head (metta-equation
      (tptp-v1:build "comma_general_term" ?alt ?start ?stop ?kids)
      (tptp-v1:node (tptp-rec-v1:one (tptp-rec-v1:nodes ?kids))))) (body))
    (rule compact-build-parents (head (metta-equation
      (tptp-v1:build "parents" ?alt ?start ?stop ?kids)
      (tptp-v1:bracketed-sequence
        (tptp-rec-v1:nodes ?kids)))) (body))
    (rule compact-build-parent-list (head (metta-equation
      (tptp-v1:build "parent_list" ?alt ?start ?stop ?kids)
      (tptp-v1:general-terms (tptp-rec-v1:nodes ?kids)))) (body))
    (rule compact-build-comma-parent (head (metta-equation
      (tptp-v1:build "comma_parent_info" ?alt ?start ?stop ?kids)
      (tptp-v1:node (tptp-rec-v1:one (tptp-rec-v1:nodes ?kids))))) (body))
    (rule compact-build-parent-info (head (metta-equation
      (tptp-v1:build "parent_info" ?alt ?start ?stop ?kids)
      (tptp-v1:node (tptp-rec-v1:head (tptp-rec-v1:nodes ?kids))))) (body))
    (rule compact-build-parent-details (head (metta-equation
      (tptp-v1:build "parent_details" ?alt ?start ?stop ?kids)
      (tptp-v1:optional (tptp-rec-v1:nodes ?kids)))) (body))

    (rule compact-build-plain-term (head (metta-equation
      (tptp-v1:build "fof_plain_term" ?alt ?start ?stop ?kids)
      (tptp-v1:plain-term (tptp-rec-v1:nodes ?kids)))) (body))
    (rule compact-build-defined-plain-term (head (metta-equation
      (tptp-v1:build "fof_defined_plain_term" ?alt ?start ?stop ?kids)
      (tptp-v1:node (tptp-rec-v1:one (tptp-rec-v1:nodes ?kids))))) (body))
    (rule compact-build-number (head (metta-equation
      (tptp-v1:build "number" ?alt ?start ?stop ?kids)
      (tptp-v1:word (tptp-rec-v1:first ?kids)))) (body))
    (rule compact-build-arguments (head (metta-equation
      (tptp-v1:build "fof_arguments" ?alt ?start ?stop ?kids)
      (tptp-v1:arguments (tptp-rec-v1:nodes ?kids)))) (body))
    (rule compact-build-comma-term (head (metta-equation
      (tptp-v1:build "comma_fof_term" ?alt ?start ?stop ?kids)
      (tptp-v1:node
        (tptp-rec-v1:one (tptp-rec-v1:nodes ?kids))))) (body))
    (rule compact-build-plain-atomic (head (metta-equation
      (tptp-v1:build "fof_plain_atomic_formula" ?alt ?start ?stop ?kids)
      (tptp-v1:atom
        (tptp-v1:node
          (tptp-rec-v1:one (tptp-rec-v1:nodes ?kids)))))) (body))
    (rule compact-build-defined-plain (head (metta-equation
      (tptp-v1:build "fof_defined_plain_formula" ?alt ?start ?stop ?kids)
      (tptp-v1:atom
        (tptp-v1:node
          (tptp-rec-v1:one (tptp-rec-v1:nodes ?kids)))))) (body))
    (rule compact-build-defined-infix (head (metta-equation
      (tptp-v1:build "fof_defined_infix_formula" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (tptp-v1:binary "="
          (tptp-v1:node (tptp-rec-v1:head ?pair))
          (tptp-v1:node (tptp-rec-v1:second ?pair)))))) (body))
    (rule compact-build-infix-unary (head (metta-equation
      (tptp-v1:build "fof_infix_unary" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (tptp-v1:binary "!="
          (tptp-v1:node (tptp-rec-v1:head ?pair))
          (tptp-v1:node (tptp-rec-v1:second ?pair)))))) (body))
    (rule compact-build-or (head (metta-equation
      (tptp-v1:build "fof_or_formula" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (tptp-v1:pair "or"
          (tptp-rec-v1:head ?pair) (tptp-rec-v1:second ?pair))))) (body))
    (rule compact-build-and (head (metta-equation
      (tptp-v1:build "fof_and_formula" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (tptp-v1:pair "and"
          (tptp-rec-v1:head ?pair) (tptp-rec-v1:second ?pair))))) (body))
    (rule compact-build-nonassoc (head (metta-equation
      (tptp-v1:build "fof_binary_nonassoc" ?alt ?start ?stop ?kids)
      (let ?nodes (tptp-rec-v1:nodes ?kids)
        (tptp-v1:binary
          (tptp-rec-v1:op (tptp-rec-v1:second ?nodes))
          (tptp-v1:node (tptp-rec-v1:head ?nodes))
          (tptp-v1:node (tptp-rec-v1:third ?nodes)))))) (body))
    (rule compact-build-unary (head (metta-equation
      (tptp-v1:build "fof_unary_formula" ?alt ?start ?stop ?kids)
      (tptp-v1:unary-form (tptp-rec-v1:nodes ?kids)))) (body))
    (rule compact-build-variable-list (head (metta-equation
      (tptp-v1:build "fof_variable_list" ?alt ?start ?stop ?kids)
      (tptp-v1:variable-seq (tptp-rec-v1:nodes ?kids)))) (body))
    (rule compact-build-comma-variable (head (metta-equation
      (tptp-v1:build "comma_variable" ?alt ?start ?stop ?kids)
      (tptp-v1:node
        (tptp-rec-v1:one (tptp-rec-v1:nodes ?kids))))) (body))
    (rule compact-build-quantified (head (metta-equation
      (tptp-v1:build "fof_quantified_formula" ?alt ?start ?stop ?kids)
      (tptp-v1:quantified
        (tptp-rec-v1:op
          (tptp-rec-v1:child "fof_quantifier"
            (tptp-rec-v1:nodes ?kids)))
        (tptp-v1:node
          (tptp-rec-v1:child "fof_variable_list"
            (tptp-rec-v1:nodes ?kids)))
        (tptp-v1:node
          (tptp-rec-v1:child "fof_unit_formula"
            (tptp-rec-v1:nodes ?kids)))))) (body))
    (rule compact-build-cnf-disjunction (head (metta-equation
      (tptp-v1:build "cnf_disjunction" ?alt ?start ?stop ?kids)
      (tptp-v1:disjunction (tptp-rec-v1:nodes ?kids)))) (body))
    (rule compact-build-cnf-literal (head (metta-equation
      (tptp-v1:build "cnf_literal" ?alt ?start ?stop ?kids)
      (tptp-v1:negated ?kids
        (tptp-v1:node
          (tptp-rec-v1:one (tptp-rec-v1:nodes ?kids)))))) (body))

    (rule compact-build-tff-typing (head (metta-equation
      (tptp-v1:build "tff_atom_typing" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (:
          (tptp-v1:node (tptp-rec-v1:head ?pair))
          (tptp-v1:node (tptp-rec-v1:second ?pair)))))) (body))
    (rule compact-build-tff-mapping (head (metta-equation
      (tptp-v1:build "tff_mapping_type" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (⇒
          (tptp-v1:node (tptp-rec-v1:head ?pair))
          (tptp-v1:node (tptp-rec-v1:second ?pair)))))) (body))
    (rule compact-build-tff-xprod (head (metta-equation
      (tptp-v1:build "tff_xprod_type" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (×
          (tptp-v1:node (tptp-rec-v1:head ?pair))
          (tptp-v1:node (tptp-rec-v1:second ?pair)))))) (body))
    (rule compact-build-tff-type-list (head (metta-equation
      (tptp-v1:build "tff_type_list" ?alt ?start ?stop ?kids)
      (tptp-v1:type-seq (tptp-rec-v1:nodes ?kids)))) (body))
    (rule compact-type-seq-one (head (metta-equation
      (tptp-v1:type-seq (LCons ?only LNil))
      (LCons (tptp-v1:node ?only) LNil))) (body))
    (rule compact-type-seq-rest (head (metta-equation
      (tptp-v1:type-seq (LCons ?first (LCons ?rest LNil)))
      (LCons (tptp-v1:node ?first) (tptp-v1:node ?rest)))) (body))
    (rule compact-build-txf-tuple-type (head (metta-equation
      (tptp-v1:build "txf_tuple_type" ?alt ?start ?stop ?kids)
      (⟨⟩
        (langdef:list->expression
          (tptp-v1:node
            (tptp-rec-v1:child "tff_type_list"
              (tptp-rec-v1:nodes ?kids))))))) (body))
    (rule compact-build-tff-atomic-type (head (metta-equation
      (tptp-v1:build "tff_atomic_type" ?alt ?start ?stop ?kids)
      (tptp-v1:plain-term (tptp-rec-v1:nodes ?kids)))) (body))
    (rule compact-build-tff-plain-atomic (head (metta-equation
      (tptp-v1:build "tff_plain_atomic" ?alt ?start ?stop ?kids)
      (tptp-v1:atom
        (tptp-v1:plain-term (tptp-rec-v1:nodes ?kids))))) (body))
    (rule compact-build-tff-defined-plain (head (metta-equation
      (tptp-v1:build "tff_defined_plain" ?alt ?start ?stop ?kids)
      (tptp-v1:defined-plain
        (tptp-v1:head-name (tptp-rec-v1:nodes ?kids))
        (tptp-rec-v1:nodes ?kids)))) (body))
    (rule compact-head-name (head (metta-equation
      (tptp-v1:head-name
        (LCons
          (tptp-cst:spanned-node
            (tptp-cst:production ?name ?alt) ?start ?stop ?kids)
          ?rest))
      ?name)) (body))
    (rule compact-defined-plain (head (metta-equation
      (tptp-v1:defined-plain ?name ?nodes)
      (tptp-v1:defined-plain-next
        (langdef:ground-equal ?name "txf_let") ?name ?nodes))) (body))
    (rule compact-defined-plain-let (head (metta-equation
      (tptp-v1:defined-plain-next LangDef:SameValue ?name ?nodes)
      (tptp-v1:node (tptp-rec-v1:head ?nodes)))) (body))
    (rule compact-defined-plain-not-let (head (metta-equation
      (tptp-v1:defined-plain-next LangDef:DifferentValue ?name ?nodes)
      (tptp-v1:defined-plain-nxf
        (langdef:ground-equal ?name "nxf_atom") ?nodes))) (body))
    (rule compact-defined-plain-nxf (head (metta-equation
      (tptp-v1:defined-plain-nxf LangDef:SameValue ?nodes)
      (tptp-v1:node (tptp-rec-v1:head ?nodes)))) (body))
    (rule compact-defined-plain-ordinary (head (metta-equation
      (tptp-v1:defined-plain-nxf LangDef:DifferentValue ?nodes)
      (tptp-v1:atom (tptp-v1:plain-term ?nodes)))) (body))
    (rule compact-build-tff-arguments (head (metta-equation
      (tptp-v1:build "tff_arguments" ?alt ?start ?stop ?kids)
      (tptp-v1:flat-items (tptp-rec-v1:nodes ?kids)))) (body))
    (rule compact-build-comma-tff (head (metta-equation
      (tptp-v1:build "comma_tff_term" ?alt ?start ?stop ?kids)
      (tptp-v1:node
        (tptp-rec-v1:one (tptp-rec-v1:nodes ?kids))))) (body))
    (rule compact-build-tff-variable-list (head (metta-equation
      (tptp-v1:build "tff_variable_list" ?alt ?start ?stop ?kids)
      (tptp-v1:variable-seq (tptp-rec-v1:nodes ?kids)))) (body))
    (rule compact-build-tff-variable (head (metta-equation
      (tptp-v1:build "tff_variable" ?alt ?start ?stop ?kids)
      (tptp-v1:node
        (tptp-rec-v1:one (tptp-rec-v1:nodes ?kids))))) (body))
    (rule compact-build-tff-typed-variable (head (metta-equation
      (tptp-v1:build "tff_typed_variable" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (:
          (tptp-v1:node (tptp-rec-v1:head ?pair))
          (tptp-v1:node (tptp-rec-v1:second ?pair)))))) (body))
    (rule compact-build-tff-quantified (head (metta-equation
      (tptp-v1:build "tff_quantified_formula" ?alt ?start ?stop ?kids)
      (tptp-v1:quantified
        (tptp-rec-v1:op
          (tptp-rec-v1:child "tff_quantifier"
            (tptp-rec-v1:nodes ?kids)))
        (tptp-v1:node
          (tptp-rec-v1:child "tff_variable_list"
            (tptp-rec-v1:nodes ?kids)))
        (tptp-v1:node
          (tptp-rec-v1:child "tff_unit_formula"
            (tptp-rec-v1:nodes ?kids)))))) (body))
    (rule compact-build-tff-or (head (metta-equation
      (tptp-v1:build "tff_or_formula" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (tptp-v1:pair "or"
          (tptp-rec-v1:head ?pair) (tptp-rec-v1:second ?pair))))) (body))
    (rule compact-build-tff-and (head (metta-equation
      (tptp-v1:build "tff_and_formula" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (tptp-v1:pair "and"
          (tptp-rec-v1:head ?pair) (tptp-rec-v1:second ?pair))))) (body))
    (rule compact-build-tff-nonassoc (head (metta-equation
      (tptp-v1:build "tff_binary_nonassoc" ?alt ?start ?stop ?kids)
      (let ?nodes (tptp-rec-v1:nodes ?kids)
        (tptp-v1:binary
          (tptp-rec-v1:op (tptp-rec-v1:second ?nodes))
          (tptp-v1:node (tptp-rec-v1:head ?nodes))
          (tptp-v1:node (tptp-rec-v1:third ?nodes)))))) (body))
    (rule compact-build-tff-prefix-unary (head (metta-equation
      (tptp-v1:build "tff_prefix_unary" ?alt ?start ?stop ?kids)
      (tptp-v1:unary-form (tptp-rec-v1:nodes ?kids)))) (body))
    (rule compact-build-tff-infix-unary (head (metta-equation
      (tptp-v1:build "tff_infix_unary" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (≉
          (tptp-v1:node (tptp-rec-v1:head ?pair))
          (tptp-v1:node (tptp-rec-v1:second ?pair)))))) (body))
    (rule compact-build-tff-defined-infix (head (metta-equation
      (tptp-v1:build "tff_defined_infix" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (≈
          (tptp-v1:node (tptp-rec-v1:head ?pair))
          (tptp-v1:node (tptp-rec-v1:second ?pair)))))) (body))
    (rule compact-build-txf-tuple (head (metta-equation
      (tptp-v1:build "txf_tuple" ?alt ?start ?stop ?kids)
      (tptp-v1:tuple-value (tptp-rec-v1:nodes ?kids)))) (body))
    (rule compact-build-tff-atom-typing-list (head (metta-equation
      (tptp-v1:build "tff_atom_typing_list" ?alt ?start ?stop ?kids)
      (langdef:list->expression
        (tptp-v1:nested-items (tptp-rec-v1:nodes ?kids))))) (body))
    (rule compact-build-txf-let-defn-list (head (metta-equation
      (tptp-v1:build "txf_let_defn_list" ?alt ?start ?stop ?kids)
      (langdef:list->expression
        (tptp-v1:nested-items (tptp-rec-v1:nodes ?kids))))) (body))
    (rule compact-build-txf-let-defn (head (metta-equation
      (tptp-v1:build "txf_let_defn" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (≔
          (tptp-v1:node (tptp-rec-v1:head ?pair))
          (tptp-v1:node (tptp-rec-v1:second ?pair)))))) (body))
    (rule compact-build-txf-let (head (metta-equation
      (tptp-v1:build "txf_let" ?alt ?start ?stop ?kids)
      (let ?nodes (tptp-rec-v1:nodes ?kids)
        (§let
          (tptp-v1:node (tptp-rec-v1:head ?nodes))
          (tptp-v1:node (tptp-rec-v1:second ?nodes))
          (tptp-v1:node (tptp-rec-v1:third ?nodes)))))) (body))
    (rule compact-build-tcf-quantified (head (metta-equation
      (tptp-v1:build "tcf_quantified_formula" ?alt ?start ?stop ?kids)
      (∀
        (langdef:list->expression
          (tptp-v1:node
            (tptp-rec-v1:child "tff_variable_list"
              (tptp-rec-v1:nodes ?kids))))
        (tptp-v1:node
          (tptp-rec-v1:child "tcf_logic_formula"
            (tptp-rec-v1:nodes ?kids)))))) (body))

    (rule compact-build-nxf-long (head (metta-equation
      (tptp-v1:build "nxf_long_connective" ?alt ?start ?stop ?kids)
      (tptp-v1:node
        (tptp-rec-v1:one (tptp-rec-v1:nodes ?kids))))) (body))
    (rule compact-build-nhf-long (head (metta-equation
      (tptp-v1:build "nhf_long_connective" ?alt ?start ?stop ?kids)
      (tptp-v1:node
        (tptp-rec-v1:one (tptp-rec-v1:nodes ?kids))))) (body))
    (rule compact-build-nxf-atom (head (metta-equation
      (tptp-v1:build "nxf_atom" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (langdef:list->expression
          (tptp-v1:append
            (langdef:expression-or-single->list
              (tptp-v1:node (tptp-rec-v1:head ?pair)))
            (tptp-v1:node (tptp-rec-v1:second ?pair))))))) (body))

    (rule compact-build-defined-type (head (metta-equation
      (tptp-v1:build "defined_type" ?alt ?start ?stop ?kids)
      (tptp-v1:word (tptp-rec-v1:first ?kids)))) (body))
    (rule compact-build-thf-typing (head (metta-equation
      (tptp-v1:build "thf_atom_typing" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (:
          (tptp-v1:node (tptp-rec-v1:head ?pair))
          (tptp-v1:node (tptp-rec-v1:second ?pair)))))) (body))
    (rule compact-build-thf-mapping (head (metta-equation
      (tptp-v1:build "thf_mapping_type" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (⇒
          (tptp-v1:node (tptp-rec-v1:head ?pair))
          (tptp-v1:node (tptp-rec-v1:second ?pair)))))) (body))
    (rule compact-build-thf-plain-atomic (head (metta-equation
      (tptp-v1:build "thf_plain_atomic" ?alt ?start ?stop ?kids)
      (tptp-v1:atom
        (tptp-v1:plain-term (tptp-rec-v1:nodes ?kids))))) (body))
    (rule compact-build-thf-apply (head (metta-equation
      (tptp-v1:build "thf_apply_formula" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (langdef:list->expression
          (tptp-v1:append-one
            (langdef:expression-or-single->list
              (tptp-v1:node (tptp-rec-v1:head ?pair)))
            (tptp-v1:node (tptp-rec-v1:second ?pair))))))) (body))
    (rule compact-build-thf-quantification (head (metta-equation
      (tptp-v1:build "thf_quantification" ?alt ?start ?stop ?kids)
      (LCons
        (tptp-rec-v1:op
          (tptp-rec-v1:child "thf_quantifier" (tptp-rec-v1:nodes ?kids)))
        (LCons
          (tptp-v1:node
            (tptp-rec-v1:child "thf_variable_list"
              (tptp-rec-v1:nodes ?kids))) LNil)))) (body))
    (rule compact-build-thf-quantified (head (metta-equation
      (tptp-v1:build "thf_quantified_formula" ?alt ?start ?stop ?kids)
      (tptp-v1:quantified-of
        (tptp-v1:node
          (tptp-rec-v1:child "thf_quantification"
            (tptp-rec-v1:nodes ?kids)))
        (tptp-v1:node
          (tptp-rec-v1:child "thf_unit_formula"
            (tptp-rec-v1:nodes ?kids)))))) (body))
    (rule compact-build-thf-variable-list (head (metta-equation
      (tptp-v1:build "thf_variable_list" ?alt ?start ?stop ?kids)
      (tptp-v1:variable-seq (tptp-rec-v1:nodes ?kids)))) (body))
    (rule compact-build-thf-typed-variable (head (metta-equation
      (tptp-v1:build "thf_typed_variable" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (:
          (tptp-v1:node (tptp-rec-v1:head ?pair))
          (tptp-v1:node (tptp-rec-v1:second ?pair)))))) (body))
    (rule compact-build-thf-and (head (metta-equation
      (tptp-v1:build "thf_and_formula" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (tptp-v1:pair "and"
          (tptp-rec-v1:head ?pair) (tptp-rec-v1:second ?pair))))) (body))
    (rule compact-build-thf-defined-infix (head (metta-equation
      (tptp-v1:build "thf_defined_infix" ?alt ?start ?stop ?kids)
      (let ?pair (tptp-rec-v1:operands (tptp-rec-v1:nodes ?kids))
        (≈
          (tptp-v1:node (tptp-rec-v1:head ?pair))
          (tptp-v1:node (tptp-rec-v1:second ?pair)))))) (body))
    (rule compact-build-thf-nonassoc (head (metta-equation
      (tptp-v1:build "thf_binary_nonassoc" ?alt ?start ?stop ?kids)
      (let ?nodes (tptp-rec-v1:nodes ?kids)
        (tptp-v1:binary
          (tptp-rec-v1:op (tptp-rec-v1:second ?nodes))
          (tptp-v1:node (tptp-rec-v1:head ?nodes))
          (tptp-v1:node (tptp-rec-v1:third ?nodes)))))) (body))))
