; The canonical terms of compact TPTP values: the inverse of
; canonical_to_compact_v1.metta up to its declared identifications
; (parentheses, the @ spine, one-literal clauses, single let typings and
; definitions, connective terms applied, numeric values, cat and 'cat').
;
; A compact value says which leaf it is under every policy, so one decoding
; serves tagged, mixed and native values: §w and §§w are $w and $$w, ?V is the
; variable V, a symbol with an upper-case initial is a variable, any other
; symbol a word, a string a distinct object, and tagged forms name themselves.
; A formula decodes by its family.  Each node is the grammar's own production
; for its form; the printer puts a node the position's rules do not reach
; inside the parentheses the grammar writes around it.  Loaded with
; canonical_to_compact_v1.metta, whose spelling helpers it uses, and the
; profile's companion file.
(gslt-presentation-v1 TptpCanonicalFromCompactV1
  (signature
    (operator : 2)
    (operator LCons 2)
    (operator bnf 1)
    (operator bnf 2)
    (operator bnf 3)
    (operator bnf 4)
    (operator bnf 5)
    (operator metta-equation 2)
    (operator tptp-canonical:annotations 1)
    (operator tptp-canonical:applied 2)
    (operator tptp-canonical:apply-fn 2)
    (operator tptp-canonical:bracketed-general 1)
    (operator tptp-canonical:chain 3)
    (operator tptp-canonical:chain-rest 3)
    (operator tptp-canonical:clause 1)
    (operator tptp-canonical:clause-list 1)
    (operator tptp-canonical:compound-key 3)
    (operator tptp-canonical:connective-or-apply 1)
    (operator tptp-canonical:defined-key 1)
    (operator tptp-canonical:defined-key-of 1)
    (operator tptp-canonical:defined-keys 0)
    (operator tptp-canonical:defined-leaf 1)
    (operator tptp-canonical:defined-leaf-of 1)
    (operator tptp-canonical:defined-sigil 1)
    (operator tptp-canonical:defined-token 1)
    (operator tptp-canonical:defined-token-at 2)
    (operator tptp-canonical:digits 1)
    (operator tptp-canonical:truth 1) (operator tptp-canonical:and 2)
    (operator tptp-canonical:after 2) (operator tptp-canonical:lower-word-token 2)
    (operator str:all-in? 2) (operator str:byte-length 1) (operator str:byte-slice 3)
    (operator str:concat 2) (operator str:escape 2) (operator str:lower-word? 1)
    (operator str:set 1) (operator str:set-member? 2) (operator str:starts-with 2)
    (operator str:symbol->string 1) (operator str:upper-initial? 1)
    (operator tptp-canonical:disjunction 1)
    (operator tptp-canonical:each 2)
    (operator tptp-canonical:file 1)
    (operator tptp-canonical:file-name 1)
    (operator tptp-canonical:first 1)
    (operator tptp-canonical:fof 1)
    (operator tptp-canonical:fof-apply 2)
    (operator tptp-canonical:fof-apply-as 3)
    (operator tptp-canonical:fof-form 3)
    (operator tptp-canonical:fof-tuple 1)
    (operator tptp-canonical:key-at 2)
    (operator tptp-canonical:key-at-let 3)
    (operator tptp-canonical:each-item 2)
    (operator tptp-canonical:item-is 2)
    (operator tptp-canonical:items-are 2)
    (operator tptp-canonical:let-key 1)
    (operator tptp-canonical:let-shaped 1)
    (operator tptp-canonical:same 1)
    (operator tptp-canonical:shape-operator 1)
    (operator tptp-canonical:formula-heads 0)
    (operator tptp-canonical:found 1)
    (operator tptp-canonical:general 1)
    (operator tptp-canonical:general-form 4)
    (operator tptp-canonical:general-heads 0)
    (operator tptp-canonical:include-optionals 1)
    (operator tptp-canonical:introduced 1)
    (operator tptp-canonical:items 5)
    (operator tptp-canonical:key 2)
    (operator tptp-canonical:key-of 3)
    (operator tptp-canonical:keywords 0)
    (operator tptp-canonical:leaf 1)
    (operator tptp-canonical:leaf-heads 0)
    (operator tptp-canonical:leaf-of 2)
    (operator tptp-canonical:literal 2)
    (operator tptp-canonical:literal-at 2)
    (operator tptp-canonical:literal-of 2)
    (operator tptp-canonical:long-connective 2)
    (operator tptp-canonical:long-connective-list 2)
    (operator tptp-canonical:lookup 2)
    (operator tptp-canonical:lookup-at 4)
    (operator tptp-canonical:map 2)
    (operator tptp-canonical:not 1)
    (operator tptp-canonical:pair 2)
    (operator tptp-canonical:parameter 5)
    (operator tptp-canonical:parent 1)
    (operator tptp-canonical:parent-at 2)
    (operator tptp-canonical:parent-list 1)
    (operator tptp-canonical:parents 1)
    (operator tptp-canonical:quantifier-literals 0)
    (operator tptp-canonical:quoted 2)
    (operator tptp-canonical:rational-text 2)
    (operator tptp-canonical:rebuild 2)
    (operator tptp-canonical:record 1)
    (operator tptp-canonical:role 1)
    (operator tptp-canonical:role-of 2)
    (operator tptp-canonical:selection 1)
    (operator tptp-canonical:selection-of 2)
    (operator tptp-canonical:shape-key 2)
    (operator tptp-canonical:shape-key-of 2)
    (operator tptp-canonical:sigil-at 2)
    (operator tptp-canonical:sigil-leaf 1)
    (operator tptp-canonical:source 1)
    (operator tptp-canonical:source-form 4)
    (operator tptp-canonical:source-heads 0)
    (operator tptp-canonical:source-leaf 2)
    (operator tptp-canonical:spelled-at 4)
    (operator tptp-canonical:spelled-key 1)
    (operator tptp-canonical:spelled-key-at 2)
    (operator tptp-canonical:spelled-leaf 1)
    (operator tptp-canonical:symbol-key 2)
    (operator tptp-canonical:symbol-leaf 2)
    (operator tptp-canonical:symbol-literals 0)
    (operator tptp-canonical:tagged-leaf 1)
    (operator tptp-canonical:tcf 1)
    (operator tptp-canonical:tcf-form 4)
    (operator tptp-canonical:tff 1)
    (operator tptp-canonical:tff-apply 2)
    (operator tptp-canonical:tff-apply-as 3)
    (operator tptp-canonical:tff-arguments 1)
    (operator tptp-canonical:tff-form 3)
    (operator tptp-canonical:tff-type 1)
    (operator tptp-canonical:tff-type-form 3)
    (operator tptp-canonical:tff-variable-form 3)
    (operator tptp-canonical:thf 1)
    (operator tptp-canonical:thf-form 3)
    (operator tptp-canonical:thf-mapping 3)
    (operator tptp-canonical:thf-variable-form 3)
    (operator tptp-canonical:useful-info 1)
    (operator tptp-canonical:value-key 2)
    (operator tptp-canonical:word-token 1)
    (operator tptp-canonical:word-token-at 2)
    (operator ¬ 1)
    (operator ≔ 2)
    (operator LangDef:ExpressionValue 1)
    (operator LangDef:ListValue 1)
    (operator LangDef:FloatValue 1)
    (operator LangDef:IntegerValue 1)
    (operator LangDef:StringValue 1)
    (operator LangDef:SymbolValue 1)
    (operator langdef:expression->list 1)
    (operator langdef:ground-equal 2)
    (operator langdef:value-view 1)
    (operator tptp-compact:expression 1))
  (equations)
  (rewrites
    ; ------------------------------------------------------------ entry
    (rule inverse-file (head (metta-equation (tptp-canonical:file ?records) (tptp-canonical:each input ?records))) (body))
    (rule input (head (metta-equation (tptp-canonical:apply-fn input ?record) (tptp-canonical:record (langdef:expression->list ?record)))) (body))

    ; ------------------------------------------------------- list helpers
    (rule inverse-map-nil (head (metta-equation (tptp-canonical:map ?function LNil) LNil)) (body))
    (rule inverse-map-cons
      (head (metta-equation (tptp-canonical:map ?function (LCons ?item ?rest))
                            (LCons (tptp-canonical:apply-fn ?function ?item) (tptp-canonical:map ?function ?rest))))
      (body))
    (rule each
      (head (metta-equation (tptp-canonical:each ?function ?expression)
                            (tptp-compact:list-value (tptp-canonical:map ?function (langdef:expression->list ?expression)))))
      (body))
    ; a b c... as first (, x)*: the head, and the repetition's list node of the
    ; comma items of the rest.
    (rule inverse-items
      (head (metta-equation (tptp-canonical:items ?constructor ?tail ?function ?comma (LCons ?first ?rest))
                            (bnf ?constructor (tptp-canonical:apply-fn ?function ?first)
                                 (bnf ?tail (tptp-compact:list-value (tptp-canonical:map ?comma ?rest))))))
      (body))
    ; x1 x2 x3... as the left-nested chain (next (first x1 x2) x3)...
    (rule chain
      (head (metta-equation (tptp-canonical:chain ?first ?next (LCons ?a (LCons ?b ?rest)))
                            (tptp-canonical:chain-rest ?next (bnf ?first ?a ?b) ?rest)))
      (body))
    (rule chain-done (head (metta-equation (tptp-canonical:chain-rest ?next ?chain LNil) ?chain)) (body))
    (rule chain-more
      (head (metta-equation (tptp-canonical:chain-rest ?next ?chain (LCons ?item ?rest))
                            (tptp-canonical:chain-rest ?next (bnf ?next ?chain ?item) ?rest)))
      (body))
    (rule first (head (metta-equation (tptp-canonical:first (LCons ?item ?rest)) ?item)) (body))
    (rule lookup-absent (head (metta-equation (tptp-canonical:lookup ?key LNil) tptp-canonical:absent)) (body))
    (rule lookup-next
      (head (metta-equation (tptp-canonical:lookup ?key (LCons (tptp-canonical:pair ?candidate ?value) ?rest))
                            (tptp-canonical:lookup-at (langdef:ground-equal ?key ?candidate) ?value ?key ?rest)))
      (body))
    (rule lookup-found (head (metta-equation (tptp-canonical:lookup-at LangDef:SameValue ?value ?key ?rest) (tptp-canonical:found ?value))) (body))
    (rule lookup-rest (head (metta-equation (tptp-canonical:lookup-at LangDef:DifferentValue ?value ?key ?rest) (tptp-canonical:lookup ?key ?rest))) (body))

    ; ------------------------------------------------------------ records
    (rule record-fof
      (head (metta-equation (tptp-canonical:record (LCons fof (LCons ?name (LCons ?role (LCons ?formula ?annotations)))))
                            (bnf fof_annotated (tptp-canonical:leaf ?name) (tptp-canonical:role ?role) (tptp-canonical:fof ?formula) (tptp-canonical:annotations ?annotations))))
      (body))
    (rule record-cnf
      (head (metta-equation (tptp-canonical:record (LCons cnf (LCons ?name (LCons ?role (LCons ?formula ?annotations)))))
                            (bnf cnf_annotated (tptp-canonical:leaf ?name) (tptp-canonical:role ?role) (tptp-canonical:clause ?formula) (tptp-canonical:annotations ?annotations))))
      (body))
    (rule record-tff
      (head (metta-equation (tptp-canonical:record (LCons tff (LCons ?name (LCons ?role (LCons ?formula ?annotations)))))
                            (bnf tff_annotated (tptp-canonical:leaf ?name) (tptp-canonical:role ?role) (tptp-canonical:tff ?formula) (tptp-canonical:annotations ?annotations))))
      (body))
    (rule record-thf
      (head (metta-equation (tptp-canonical:record (LCons thf (LCons ?name (LCons ?role (LCons ?formula ?annotations)))))
                            (bnf thf_annotated (tptp-canonical:leaf ?name) (tptp-canonical:role ?role) (tptp-canonical:thf ?formula) (tptp-canonical:annotations ?annotations))))
      (body))
    (rule record-tcf
      (head (metta-equation (tptp-canonical:record (LCons tcf (LCons ?name (LCons ?role (LCons ?formula ?annotations)))))
                            (bnf tcf_annotated (tptp-canonical:leaf ?name) (tptp-canonical:role ?role) (tptp-canonical:tcf ?formula) (tptp-canonical:annotations ?annotations))))
      (body))
    (rule record-tpi
      (head (metta-equation (tptp-canonical:record (LCons tpi (LCons ?name (LCons ?role (LCons ?formula ?annotations)))))
                            (bnf tpi_annotated (tptp-canonical:leaf ?name) (tptp-canonical:role ?role) (tptp-canonical:fof ?formula) (tptp-canonical:annotations ?annotations))))
      (body))
    (rule record-incl
      (head (metta-equation (tptp-canonical:record (LCons incl (LCons ?file ?optionals)))
                            (bnf include (tptp-canonical:file-name ?file) (tptp-canonical:include-optionals ?optionals))))
      (body))
    (rule annotations-none (head (metta-equation (tptp-canonical:annotations LNil) (bnf nothing))) (body))
    (rule annotations-source
      (head (metta-equation (tptp-canonical:annotations (LCons ?source LNil))
                            (bnf annotations (tptp-canonical:source ?source) (bnf nothing))))
      (body))
    (rule annotations-info
      (head (metta-equation (tptp-canonical:annotations (LCons ?source (LCons ?info LNil)))
                            (bnf annotations (tptp-canonical:source ?source) (bnf optional_info (tptp-canonical:useful-info ?info)))))
      (body))
    (rule include-plain (head (metta-equation (tptp-canonical:include-optionals LNil) (bnf nothing))) (body))
    (rule include-selection
      (head (metta-equation (tptp-canonical:include-optionals (LCons ?selection LNil))
                            (bnf include_optionals/1 (tptp-canonical:selection ?selection))))
      (body))
    (rule include-space
      (head (metta-equation (tptp-canonical:include-optionals (LCons ?selection (LCons ?space LNil)))
                            (bnf include_optionals/2 (tptp-canonical:selection ?selection) (tptp-canonical:leaf ?space))))
      (body))
    (rule selection (head (metta-equation (tptp-canonical:selection ?selection) (tptp-canonical:selection-of (langdef:value-view ?selection) ?selection))) (body))
    (rule selection-all (head (metta-equation (tptp-canonical:selection-of (LangDef:SymbolValue ?spelling) ?selection) "*")) (body))
    (rule selection-names
      (head (metta-equation (tptp-canonical:selection-of (LangDef:ListValue ?names) ?selection)
                            (bnf formula_selection (bnf name_list (tptp-canonical:each leaf (langdef:list->list-value ?names))))))
      (body))
    (rule role (head (metta-equation (tptp-canonical:role ?role) (tptp-canonical:role-of (langdef:value-view ?role) ?role))) (body))
    (rule inverse-role-word (head (metta-equation (tptp-canonical:role-of (LangDef:SymbolValue ?spelling) ?role) (bnf lower_word ?spelling))) (body))
    (rule inverse-role-refined
      (head (metta-equation (tptp-canonical:role-of (LangDef:ExpressionValue (LCons ?word (LCons ?term LNil))) ?role)
                            (bnf formula_role (bnf lower_word (str:symbol->string ?word)) (tptp-canonical:general ?term))))
      (body))
    (rule file-name (head (metta-equation (tptp-canonical:file-name ?file) (bnf single_quoted (tptp-canonical:quoted 39 ?file)))) (body))

    ; -------------------------------------------------------------- heads
    ; The form a compound value takes, read from its head.
    (rule formula-heads
      (head (metta-equation (tptp-canonical:formula-heads)
        (LCons (tptp-canonical:pair ∀ quantifier) (LCons (tptp-canonical:pair ∃ quantifier) (LCons (tptp-canonical:pair λ quantifier) (LCons (tptp-canonical:pair ε quantifier) (LCons (tptp-canonical:pair ι quantifier) (LCons (tptp-canonical:pair Π quantifier) (LCons (tptp-canonical:pair Σ quantifier) (LCons (tptp-canonical:pair ¬ prefix) (LCons (tptp-canonical:pair □ prefix) (LCons (tptp-canonical:pair ◇ prefix) (LCons (tptp-canonical:pair ↔ nonassoc) (LCons (tptp-canonical:pair → nonassoc) (LCons (tptp-canonical:pair ← nonassoc) (LCons (tptp-canonical:pair ⊕ nonassoc) (LCons (tptp-canonical:pair ⊽ nonassoc) (LCons (tptp-canonical:pair ⊼ nonassoc) (LCons (tptp-canonical:pair ∨ or) (LCons (tptp-canonical:pair ∧ and) (LCons (tptp-canonical:pair ≈ equal) (LCons (tptp-canonical:pair ≉ unequal) (LCons (tptp-canonical:pair ⊢ sequent) (LCons (tptp-canonical:pair ≡ definition) (LCons (tptp-canonical:pair : typing) (LCons (tptp-canonical:pair ⊑ subtype) (LCons (tptp-canonical:pair ⇒ mapping) (LCons (tptp-canonical:pair × product) (LCons (tptp-canonical:pair ⊎ union) (LCons (tptp-canonical:pair ≔ assignment) (tptp-canonical:leaf-heads)))))))))))))))))))))))))))))))
      (body))
    (rule source-heads
      (head (metta-equation (tptp-canonical:source-heads)
        (LCons (tptp-canonical:pair inference inference) (LCons (tptp-canonical:pair introduced introduced) (LCons (tptp-canonical:pair creator creator) (LCons (tptp-canonical:pair file file) (LCons (tptp-canonical:pair theory theory) (tptp-canonical:leaf-heads))))))))
      (body))
    (rule general-heads
      (head (metta-equation (tptp-canonical:general-heads)
        (LCons (tptp-canonical:pair : typing) (tptp-canonical:leaf-heads))))
      (body))
    (rule leaf-heads
      (head (metta-equation (tptp-canonical:leaf-heads)
        (LCons (tptp-canonical:pair word leaf) (LCons (tptp-canonical:pair var leaf) (LCons (tptp-canonical:pair defined leaf) (LCons (tptp-canonical:pair system leaf) (LCons (tptp-canonical:pair distinct leaf) (LCons (tptp-canonical:pair rat leaf) (LCons (tptp-canonical:pair real leaf) LNil)))))))))
      (body))
    ; Defined spellings with their own form.
    (rule defined-keys
      (head (metta-equation (tptp-canonical:defined-keys)
        (LCons (tptp-canonical:pair "let" let) (LCons (tptp-canonical:pair "#" hash) (LCons (tptp-canonical:pair "{.}" prefix) (LCons (tptp-canonical:pair "(.)" prefix) (LCons (tptp-canonical:pair "thf" data-thf) (LCons (tptp-canonical:pair "tff" data-tff) (LCons (tptp-canonical:pair "fof" data-fof) (LCons (tptp-canonical:pair "cnf" data-cnf) (LCons (tptp-canonical:pair "fot" data-fot) LNil)))))))))))
      (body))
    (rule key
      (head (metta-equation (tptp-canonical:key ?table ?head) (tptp-canonical:key-of (langdef:value-view ?head) ?table ?head)))
      (body))
    (rule key-symbol
      (head (metta-equation (tptp-canonical:key-of (LangDef:SymbolValue ?spelling) ?table ?head)
                            (tptp-canonical:symbol-key (tptp-canonical:lookup ?head ?table) ?spelling)))
      (body))
    (rule symbol-key-found (head (metta-equation (tptp-canonical:symbol-key (tptp-canonical:found ?key) ?spelling) ?key)) (body))
    (rule symbol-key-absent (head (metta-equation (tptp-canonical:symbol-key tptp-canonical:absent ?spelling) (tptp-canonical:spelled-key ?spelling))) (body))
    ; §w names the defined word w; any other symbol is applied.
    (rule spelled-key
      (head (metta-equation (tptp-canonical:spelled-key ?spelling)
                            (tptp-canonical:spelled-key-at (tptp-canonical:defined-sigil ?spelling) ?spelling)))
      (body))
    (rule spelled-key-defined (head (metta-equation (tptp-canonical:spelled-key-at True ?spelling) (tptp-canonical:defined-key (tptp-canonical:after ?spelling 2)))) (body))
    (rule spelled-key-other (head (metta-equation (tptp-canonical:spelled-key-at False ?spelling) apply)) (body))
    (rule key-expression
      (head (metta-equation (tptp-canonical:key-of (LangDef:ExpressionValue (LCons ?first ?rest)) ?table ?head)
                            (tptp-canonical:compound-key (langdef:ground-equal ?first defined) ?first ?rest)))
      (body))
    (rule key-empty (head (metta-equation (tptp-canonical:key-of (LangDef:ExpressionValue LNil) ?table ?head) apply)) (body))
    (rule key-list (head (metta-equation (tptp-canonical:key-of (LangDef:ListValue ?items) ?table ?head) apply)) (body))
    (rule key-string (head (metta-equation (tptp-canonical:key-of (LangDef:StringValue ?text) ?table ?head) apply)) (body))
    (rule key-integer (head (metta-equation (tptp-canonical:key-of (LangDef:IntegerValue ?text) ?table ?head) apply)) (body))
    (rule key-float (head (metta-equation (tptp-canonical:key-of (LangDef:FloatValue ?text) ?table ?head) apply)) (body))
    ; (defined "w") as a head is the defined word w; a head that is itself
    ; headed by a defined word is a long connective.
    (rule compound-defined (head (metta-equation (tptp-canonical:compound-key LangDef:SameValue ?first (LCons ?word LNil)) (tptp-canonical:defined-key ?word))) (body))
    (rule compound-other (head (metta-equation (tptp-canonical:compound-key LangDef:DifferentValue ?first ?rest) (tptp-canonical:connective-or-apply (tptp-canonical:defined-leaf ?first)))) (body))
    (rule connective-key (head (metta-equation (tptp-canonical:connective-or-apply True) long-connective)) (body))
    (rule apply-key (head (metta-equation (tptp-canonical:connective-or-apply False) apply)) (body))
    (rule defined-key (head (metta-equation (tptp-canonical:defined-key ?word) (tptp-canonical:defined-key-of (tptp-canonical:lookup ?word (tptp-canonical:defined-keys))))) (body))
    (rule defined-key-found (head (metta-equation (tptp-canonical:defined-key-of (tptp-canonical:found ?key)) ?key)) (body))
    (rule defined-key-absent (head (metta-equation (tptp-canonical:defined-key-of tptp-canonical:absent) apply)) (body))
    ; Whether a value is a defined word, §w or (defined "w").
    (rule defined-leaf (head (metta-equation (tptp-canonical:defined-leaf ?value) (tptp-canonical:defined-leaf-of (langdef:value-view ?value)))) (body))
    (rule defined-leaf-symbol (head (metta-equation (tptp-canonical:defined-leaf-of (LangDef:SymbolValue ?spelling)) (tptp-canonical:defined-sigil ?spelling))) (body))
    (rule defined-leaf-expression (head (metta-equation (tptp-canonical:defined-leaf-of (LangDef:ExpressionValue (LCons ?first ?rest))) (tptp-canonical:truth (langdef:ground-equal ?first defined)))) (body))
    (rule defined-leaf-empty (head (metta-equation (tptp-canonical:defined-leaf-of (LangDef:ExpressionValue LNil)) False)) (body))
    (rule defined-leaf-list (head (metta-equation (tptp-canonical:defined-leaf-of (LangDef:ListValue ?items)) False)) (body))
    (rule defined-leaf-string (head (metta-equation (tptp-canonical:defined-leaf-of (LangDef:StringValue ?text)) False)) (body))
    (rule defined-leaf-integer (head (metta-equation (tptp-canonical:defined-leaf-of (LangDef:IntegerValue ?text)) False)) (body))
    (rule defined-leaf-float (head (metta-equation (tptp-canonical:defined-leaf-of (LangDef:FloatValue ?text)) False)) (body))
    ; § not followed by another §.
    (rule defined-sigil
      (head (metta-equation (tptp-canonical:defined-sigil ?spelling)
                            (tptp-canonical:and (str:starts-with ?spelling "§") (tptp-canonical:not (str:starts-with ?spelling "§§")))))
      (body))
    (rule not-true (head (metta-equation (tptp-canonical:not True) False)) (body))
    (rule not-false (head (metta-equation (tptp-canonical:not False) True)) (body))
    ; The key of a value: its head's key in the table for an expression, tuple
    ; for a list and leaf for a symbol, string or number.  The forms take the
    ; key, the value's view and the value, and read the head and the
    ; arguments from the view.
    (rule shape-key (head (metta-equation (tptp-canonical:shape-key ?table ?value) (tptp-canonical:shape-key-of (langdef:value-view ?value) ?table))) (body))
    (rule shape-key-compound
      (head (metta-equation (tptp-canonical:shape-key-of (LangDef:ExpressionValue (LCons ?head ?arguments)) ?table)
                            (tptp-canonical:key-at (tptp-canonical:key ?table ?head) ?arguments)))
      (body))
    ; The let key names the let form only over the let shape: a list of
    ; typings, a list of definitions and a body.  The defined word $let
    ; applied to anything else, $let(a,b), is an application, and prints as
    ; the defined functor it is.
    (rule key-at
      (head (metta-equation (tptp-canonical:key-at ?key ?arguments)
                            (tptp-canonical:key-at-let (langdef:ground-equal ?key let) ?key ?arguments)))
      (body))
    (rule key-at-other (head (metta-equation (tptp-canonical:key-at-let LangDef:DifferentValue ?key ?arguments) ?key)) (body))
    (rule key-at-let
      (head (metta-equation (tptp-canonical:key-at-let LangDef:SameValue ?key ?arguments)
                            (tptp-canonical:let-key (tptp-canonical:let-shaped ?arguments))))
      (body))
    (rule let-key-true (head (metta-equation (tptp-canonical:let-key True) let)) (body))
    (rule let-key-false (head (metta-equation (tptp-canonical:let-key False) apply)) (body))
    (rule let-shaped-0 (head (metta-equation (tptp-canonical:let-shaped LNil) False)) (body))
    (rule let-shaped-1 (head (metta-equation (tptp-canonical:let-shaped (LCons ?a LNil)) False)) (body))
    (rule let-shaped-2 (head (metta-equation (tptp-canonical:let-shaped (LCons ?a (LCons ?b LNil))) False)) (body))
    (rule let-shaped-3
      (head (metta-equation (tptp-canonical:let-shaped (LCons ?typings (LCons ?definitions (LCons ?body LNil))))
                            (tptp-canonical:and (tptp-canonical:items-are typing (langdef:value-view ?typings))
                                                (tptp-canonical:items-are definition (langdef:value-view ?definitions)))))
      (body))
    (rule let-shaped-more (head (metta-equation (tptp-canonical:let-shaped (LCons ?a (LCons ?b (LCons ?c (LCons ?d ?more))))) False)) (body))
    ; Every element of a list value has the shape: (: x T) for a typing,
    ; (≔ l r) for a definition.  Any other value is not such a list.
    (rule items-are-list (head (metta-equation (tptp-canonical:items-are ?shape (LangDef:ListValue ?items)) (tptp-canonical:each-item ?shape ?items))) (body))
    (rule items-are-symbol (head (metta-equation (tptp-canonical:items-are ?shape (LangDef:SymbolValue ?spelling)) False)) (body))
    (rule items-are-string (head (metta-equation (tptp-canonical:items-are ?shape (LangDef:StringValue ?text)) False)) (body))
    (rule items-are-integer (head (metta-equation (tptp-canonical:items-are ?shape (LangDef:IntegerValue ?text)) False)) (body))
    (rule items-are-float (head (metta-equation (tptp-canonical:items-are ?shape (LangDef:FloatValue ?text)) False)) (body))
    (rule items-are-expression (head (metta-equation (tptp-canonical:items-are ?shape (LangDef:ExpressionValue ?elements)) False)) (body))
    (rule each-item-nil (head (metta-equation (tptp-canonical:each-item ?shape LNil) True)) (body))
    (rule each-item-cons
      (head (metta-equation (tptp-canonical:each-item ?shape (LCons ?item ?rest))
                            (tptp-canonical:and (tptp-canonical:item-is ?shape (langdef:value-view ?item)) (tptp-canonical:each-item ?shape ?rest))))
      (body))
    (rule item-is-binary
      (head (metta-equation (tptp-canonical:item-is ?shape (LangDef:ExpressionValue (LCons ?operator (LCons ?left (LCons ?right LNil)))))
                            (tptp-canonical:same (langdef:ground-equal ?operator (tptp-canonical:shape-operator ?shape)))))
      (body))
    (rule item-is-nil (head (metta-equation (tptp-canonical:item-is ?shape (LangDef:ExpressionValue LNil)) False)) (body))
    (rule item-is-1 (head (metta-equation (tptp-canonical:item-is ?shape (LangDef:ExpressionValue (LCons ?a LNil))) False)) (body))
    (rule item-is-2 (head (metta-equation (tptp-canonical:item-is ?shape (LangDef:ExpressionValue (LCons ?a (LCons ?b LNil)))) False)) (body))
    (rule item-is-more (head (metta-equation (tptp-canonical:item-is ?shape (LangDef:ExpressionValue (LCons ?a (LCons ?b (LCons ?c (LCons ?d ?rest)))))) False)) (body))
    (rule item-is-list (head (metta-equation (tptp-canonical:item-is ?shape (LangDef:ListValue ?items)) False)) (body))
    (rule item-is-symbol (head (metta-equation (tptp-canonical:item-is ?shape (LangDef:SymbolValue ?spelling)) False)) (body))
    (rule item-is-string (head (metta-equation (tptp-canonical:item-is ?shape (LangDef:StringValue ?text)) False)) (body))
    (rule item-is-integer (head (metta-equation (tptp-canonical:item-is ?shape (LangDef:IntegerValue ?text)) False)) (body))
    (rule item-is-float (head (metta-equation (tptp-canonical:item-is ?shape (LangDef:FloatValue ?text)) False)) (body))
    (rule shape-operator-typing (head (metta-equation (tptp-canonical:shape-operator typing) :)) (body))
    (rule shape-operator-definition (head (metta-equation (tptp-canonical:shape-operator definition) ≔)) (body))
    (rule same-true (head (metta-equation (tptp-canonical:same LangDef:SameValue) True)) (body))
    (rule same-false (head (metta-equation (tptp-canonical:same LangDef:DifferentValue) False)) (body))
    (rule shape-key-list (head (metta-equation (tptp-canonical:shape-key-of (LangDef:ListValue ?items) ?table) tuple)) (body))
    (rule shape-key-symbol (head (metta-equation (tptp-canonical:shape-key-of (LangDef:SymbolValue ?spelling) ?table) leaf)) (body))
    (rule shape-key-string (head (metta-equation (tptp-canonical:shape-key-of (LangDef:StringValue ?text) ?table) leaf)) (body))
    (rule shape-key-integer (head (metta-equation (tptp-canonical:shape-key-of (LangDef:IntegerValue ?text) ?table) leaf)) (body))
    (rule shape-key-float (head (metta-equation (tptp-canonical:shape-key-of (LangDef:FloatValue ?text) ?table) leaf)) (body))
    (rule value-key (head (metta-equation (tptp-canonical:value-key ?table ?value) (tptp-canonical:shape-key ?table ?value))) (body))
    ; A leaf whose shape was taken apart: the atom itself, or the tagged form.
    (rule rebuild-atom (head (metta-equation (tptp-canonical:rebuild ?head LNil) ?head)) (body))
    (rule rebuild-tagged (head (metta-equation (tptp-canonical:rebuild ?head (LCons ?first ?rest)) (tptp-compact:expression (LCons ?head (LCons ?first ?rest))))) (body))

    ; -------------------------------------------------------------- leaves
    (rule leaf (head (metta-equation (tptp-canonical:leaf ?value) (tptp-canonical:leaf-of (langdef:value-view ?value) ?value))) (body))
    (rule apply-leaf (head (metta-equation (tptp-canonical:apply-fn leaf ?value) (tptp-canonical:leaf ?value))) (body))
    (rule leaf-symbol
      (head (metta-equation (tptp-canonical:leaf-of (LangDef:SymbolValue ?spelling) ?value)
                            (tptp-canonical:symbol-leaf (tptp-canonical:lookup ?value (tptp-canonical:symbol-literals)) ?spelling)))
      (body))
    (rule leaf-string (head (metta-equation (tptp-canonical:leaf-of (LangDef:StringValue ?text) ?value) (bnf distinct_object (tptp-canonical:quoted 34 ?text)))) (body))
    (rule leaf-integer (head (metta-equation (tptp-canonical:leaf-of (LangDef:IntegerValue ?digits) ?value) (bnf integer ?digits))) (body))
    (rule leaf-float (head (metta-equation (tptp-canonical:leaf-of (LangDef:FloatValue ?text) ?value) (bnf real ?text))) (body))
    (rule leaf-tagged (head (metta-equation (tptp-canonical:leaf-of (LangDef:ExpressionValue ?items) ?value) (tptp-canonical:tagged-leaf ?items))) (body))
    (rule tagged-word (head (metta-equation (tptp-canonical:tagged-leaf (LCons word (LCons ?text LNil))) (tptp-canonical:word-token ?text))) (body))
    (rule tagged-var (head (metta-equation (tptp-canonical:tagged-leaf (LCons var (LCons ?text LNil))) (bnf upper_word ?text))) (body))
    (rule tagged-defined (head (metta-equation (tptp-canonical:tagged-leaf (LCons defined (LCons ?text LNil))) (tptp-canonical:defined-token ?text))) (body))
    (rule tagged-system
      (head (metta-equation (tptp-canonical:tagged-leaf (LCons system (LCons ?text LNil)))
                            (bnf dollar_dollar_word (str:concat "$$" ?text))))
      (body))
    (rule tagged-distinct (head (metta-equation (tptp-canonical:tagged-leaf (LCons distinct (LCons ?text LNil))) (bnf distinct_object (tptp-canonical:quoted 34 ?text)))) (body))
    (rule tagged-rat (head (metta-equation (tptp-canonical:tagged-leaf (LCons rat (LCons ?n (LCons ?d LNil)))) (bnf rational (tptp-canonical:rational-text ?n ?d)))) (body))
    (rule tagged-real (head (metta-equation (tptp-canonical:tagged-leaf (LCons real (LCons ?text LNil))) (bnf real ?text))) (body))
    ; Bare glyphs and keywords at a leaf position are the grammar's literals.
    (rule symbol-literals
      (head (metta-equation (tptp-canonical:symbol-literals)
        (LCons (tptp-canonical:pair ∧ "&") (LCons (tptp-canonical:pair ∨ "|") (LCons (tptp-canonical:pair ¬ "~") (LCons (tptp-canonical:pair ↔ "<=>") (LCons (tptp-canonical:pair → "=>") (LCons (tptp-canonical:pair ← "<=") (LCons (tptp-canonical:pair ⊕ "<~>") (LCons (tptp-canonical:pair ⊽ "~|") (LCons (tptp-canonical:pair ⊼ "~&") (LCons (tptp-canonical:pair ≈ "=") (LCons (tptp-canonical:pair ≉ "!=") (LCons (tptp-canonical:pair □ "[.]") (LCons (tptp-canonical:pair ◇ "<.>") LNil)))))))))))))))
      (body))
    (rule quantifier-literals
      (head (metta-equation (tptp-canonical:quantifier-literals)
        (LCons (tptp-canonical:pair ∀ "!") (LCons (tptp-canonical:pair ∃ "?") (LCons (tptp-canonical:pair λ "^") (LCons (tptp-canonical:pair ε "@+") (LCons (tptp-canonical:pair ι "@-") (LCons (tptp-canonical:pair Π "!>") (LCons (tptp-canonical:pair Σ "?*") LNil)))))))))
      (body))
    (rule symbol-literal (head (metta-equation (tptp-canonical:symbol-leaf (tptp-canonical:found ?literal) ?spelling) ?literal)) (body))
    (rule symbol-spelled (head (metta-equation (tptp-canonical:symbol-leaf tptp-canonical:absent ?spelling) (tptp-canonical:spelled-leaf ?spelling))) (body))
    ; § and §§ are the sigils, ? the escaped variable, an upper-case initial a
    ; variable; § is two bytes.
    (rule spelled-leaf
      (head (metta-equation (tptp-canonical:spelled-leaf ?spelling)
                            (tptp-canonical:spelled-at (str:starts-with ?spelling "§") (str:starts-with ?spelling "?") (str:upper-initial? ?spelling) ?spelling)))
      (body))
    (rule spelled-sigil (head (metta-equation (tptp-canonical:spelled-at True ?question ?upper ?spelling) (tptp-canonical:sigil-leaf (tptp-canonical:after ?spelling 2)))) (body))
    (rule spelled-escaped
      (head (metta-equation (tptp-canonical:spelled-at False True ?upper ?spelling)
                            (bnf upper_word (tptp-canonical:after ?spelling 1))))
      (body))
    (rule spelled-variable
      (head (metta-equation (tptp-canonical:spelled-at False False True ?spelling)
                            (bnf upper_word ?spelling)))
      (body))
    (rule spelled-word
      (head (metta-equation (tptp-canonical:spelled-at False False False ?spelling)
                            (tptp-canonical:word-token ?spelling)))
      (body))
    (rule sigil-leaf
      (head (metta-equation (tptp-canonical:sigil-leaf ?rest)
                            (tptp-canonical:sigil-at (str:starts-with ?rest "§") ?rest)))
      (body))
    (rule sigil-system
      (head (metta-equation (tptp-canonical:sigil-at True ?rest)
                            (bnf dollar_dollar_word (str:concat "$$" (tptp-canonical:after ?rest 2)))))
      (body))
    (rule sigil-defined
      (head (metta-equation (tptp-canonical:sigil-at False ?rest)
                            (bnf dollar_word (str:concat "$" ?rest))))
      (body))
    ; The bytes of a spelling after its first `count`.
    (rule after
      (head (metta-equation (tptp-canonical:after ?spelling ?count)
                            (str:byte-slice ?spelling ?count (str:byte-length ?spelling))))
      (body))
    ; A word is a lower word when it is spelled like one and is not one of the
    ; grammar's keywords, which the reader would take as the keyword where it
    ; fits; any other word is quoted, 'cat' being cat.
    (rule word-token
      (head (metta-equation (tptp-canonical:word-token ?text)
                            (tptp-canonical:word-token-at
                              (tptp-canonical:lower-word-token
                                (str:lower-word? ?text)
                                (str:set-member? (tptp-canonical:keywords) ?text))
                              ?text)))
      (body))
    (rule keywords
      (head (metta-equation (tptp-canonical:keywords)
                            (str:set (LCons "tpi" (LCons "thf" (LCons "tff" (LCons "tcf" (LCons "fof" (LCons "cnf" (LCons "include"
                            (LCons "inference" (LCons "introduced" (LCons "file" (LCons "theory" (LCons "creator" (LCons "unknown"
                            LNil))))))))))))))))
      (body))
    (rule lower-word-token (head (metta-equation (tptp-canonical:lower-word-token True False) True)) (body))
    (rule keyword-token (head (metta-equation (tptp-canonical:lower-word-token True True) False)) (body))
    (rule other-word-token (head (metta-equation (tptp-canonical:lower-word-token False ?keyword) False)) (body))
    (rule word-lower (head (metta-equation (tptp-canonical:word-token-at True ?text) (bnf lower_word ?text))) (body))
    (rule word-quoted (head (metta-equation (tptp-canonical:word-token-at False ?text) (bnf single_quoted (tptp-canonical:quoted 39 ?text)))) (body))
    ; A defined spelling that is alphanumeric is a dollar word; any other is
    ; the grammar's literal of that spelling.
    (rule defined-token (head (metta-equation (tptp-canonical:defined-token ?text) (tptp-canonical:defined-token-at (str:all-in? word ?text) ?text))) (body))
    (rule defined-word
      (head (metta-equation (tptp-canonical:defined-token-at True ?text)
                            (bnf dollar_word (str:concat "$" ?text))))
      (body))
    (rule defined-literal (head (metta-equation (tptp-canonical:defined-token-at False ?text) ?text)) (body))
    ; "..." and '...' with \ before the quote and before \.
    (rule quoted-single
      (head (metta-equation (tptp-canonical:quoted 39 ?text)
                            (str:concat "'"
                              (str:concat (str:escape tptp-single ?text) "'"))))
      (body))
    (rule quoted-double
      (head (metta-equation (tptp-canonical:quoted 34 ?text)
                            (str:concat "\""
                              (str:concat (str:escape tptp-double ?text) "\""))))
      (body))
    (rule rational-text
      (head (metta-equation (tptp-canonical:rational-text ?numerator ?denominator)
                            (str:concat (tptp-canonical:digits (langdef:value-view ?numerator))
                              (str:concat "/" (tptp-canonical:digits (langdef:value-view ?denominator))))))
      (body))
    (rule digits (head (metta-equation (tptp-canonical:digits (LangDef:IntegerValue ?text)) ?text)) (body))
    ; The boolean connectives of the spelling tests.
    (rule truth-same (head (metta-equation (tptp-canonical:truth LangDef:SameValue) True)) (body))
    (rule truth-different (head (metta-equation (tptp-canonical:truth LangDef:DifferentValue) False)) (body))
    (rule and-true (head (metta-equation (tptp-canonical:and True ?b) ?b)) (body))
    (rule and-false (head (metta-equation (tptp-canonical:and False ?b) False)) (body))
    ; The literal of a connective or quantifier: a glyph's, or a defined spelling's.
    (rule literal (head (metta-equation (tptp-canonical:literal ?table ?connective) (tptp-canonical:literal-of (tptp-canonical:lookup ?connective ?table) ?connective))) (body))
    (rule literal-glyph (head (metta-equation (tptp-canonical:literal-of (tptp-canonical:found ?literal) ?connective) ?literal)) (body))
    (rule literal-defined (head (metta-equation (tptp-canonical:literal-of tptp-canonical:absent ?connective) (tptp-canonical:leaf ?connective))) (body))

    ; ----------------------------------------------------------------- FOF
    (rule fof (head (metta-equation (tptp-canonical:fof ?value) (tptp-canonical:fof-form (tptp-canonical:shape-key (tptp-canonical:formula-heads) ?value) (langdef:value-view ?value) ?value))) (body))
    (rule apply-fof (head (metta-equation (tptp-canonical:apply-fn fof ?value) (tptp-canonical:fof ?value))) (body))
    (rule fof-quantified
      (head (metta-equation (tptp-canonical:fof-form quantifier (LangDef:ExpressionValue (LCons ?q (LCons ?variables (LCons ?body LNil)))) ?__value)
                            (bnf fof_quantified_formula (tptp-canonical:literal (tptp-canonical:quantifier-literals) ?q) (bnf fof_variable_list (tptp-canonical:each leaf ?variables)) (tptp-canonical:fof ?body))))
      (body))
    (rule fof-unary
      (head (metta-equation (tptp-canonical:fof-form prefix (LangDef:ExpressionValue (LCons ?c (LCons ?operand LNil))) ?__value)
                            (bnf fof_unary_formula (tptp-canonical:literal (tptp-canonical:symbol-literals) ?c) (tptp-canonical:fof ?operand))))
      (body))
    (rule fof-nonassoc
      (head (metta-equation (tptp-canonical:fof-form nonassoc (LangDef:ExpressionValue (LCons ?c (LCons ?a (LCons ?b LNil)))) ?__value)
                            (bnf fof_binary_nonassoc (tptp-canonical:fof ?a) (tptp-canonical:literal (tptp-canonical:symbol-literals) ?c) (tptp-canonical:fof ?b))))
      (body))
    (rule fof-or (head (metta-equation (tptp-canonical:fof-form or (LangDef:ExpressionValue (LCons ?c ?items)) ?__value) (tptp-canonical:chain fof_or_formula/0 fof_or_formula/1 (tptp-canonical:map fof ?items)))) (body))
    (rule fof-and (head (metta-equation (tptp-canonical:fof-form and (LangDef:ExpressionValue (LCons ?c ?items)) ?__value) (tptp-canonical:chain fof_and_formula/0 fof_and_formula/1 (tptp-canonical:map fof ?items)))) (body))
    (rule fof-equal
      (head (metta-equation (tptp-canonical:fof-form equal (LangDef:ExpressionValue (LCons ?c (LCons ?a (LCons ?b LNil)))) ?__value)
                            (bnf fof_defined_infix_formula (tptp-canonical:fof ?a) "=" (tptp-canonical:fof ?b))))
      (body))
    (rule fof-unequal
      (head (metta-equation (tptp-canonical:fof-form unequal (LangDef:ExpressionValue (LCons ?c (LCons ?a (LCons ?b LNil)))) ?__value)
                            (bnf fof_infix_unary (tptp-canonical:fof ?a) "!=" (tptp-canonical:fof ?b))))
      (body))
    (rule fof-sequent
      (head (metta-equation (tptp-canonical:fof-form sequent (LangDef:ExpressionValue (LCons ?c (LCons ?a (LCons ?b LNil)))) ?__value)
                            (bnf fof_sequent/0 (tptp-canonical:fof-tuple (langdef:expression->list ?a)) "-->" (tptp-canonical:fof-tuple (langdef:expression->list ?b)))))
      (body))
    (rule fof-tuple-empty (head (metta-equation (tptp-canonical:fof-tuple LNil) "[]")) (body))
    (rule fof-tuple
      (head (metta-equation (tptp-canonical:fof-tuple (LCons ?first ?rest))
                            (bnf fof_formula_tuple (tptp-canonical:items fof_formula_tuple_list fof_formula_tuple_list*1 fof comma-fof (LCons ?first ?rest)))))
      (body))
    (rule comma-fof (head (metta-equation (tptp-canonical:apply-fn comma-fof ?value) (bnf comma_fof_logic_formula (tptp-canonical:fof ?value)))) (body))
    (rule fof-leaf (head (metta-equation (tptp-canonical:fof-form leaf ?__view ?head) (tptp-canonical:leaf (tptp-canonical:rebuild ?head LNil)))) (body))
    (rule fof-apply (head (metta-equation (tptp-canonical:fof-form apply (LangDef:ExpressionValue (LCons ?head ?arguments)) ?__value) (tptp-canonical:fof-apply (tptp-canonical:leaf ?head) (tptp-compact:list-value (tptp-canonical:map fof ?arguments))))) (body))
    (rule fof-apply-token (head (metta-equation (tptp-canonical:fof-apply (bnf ?kind ?text) ?arguments) (tptp-canonical:fof-apply-as ?kind (bnf ?kind ?text) ?arguments))) (body))
    (rule fof-plain (head (metta-equation (tptp-canonical:fof-apply-as lower_word ?head ?arguments) (bnf fof_plain_term ?head (bnf fof_arguments ?arguments)))) (body))
    (rule fof-plain-quoted (head (metta-equation (tptp-canonical:fof-apply-as single_quoted ?head ?arguments) (bnf fof_plain_term ?head (bnf fof_arguments ?arguments)))) (body))
    (rule fof-defined-apply (head (metta-equation (tptp-canonical:fof-apply-as dollar_word ?head ?arguments) (bnf fof_defined_plain_term ?head (bnf fof_arguments ?arguments)))) (body))
    (rule fof-system-apply (head (metta-equation (tptp-canonical:fof-apply-as dollar_dollar_word ?head ?arguments) (bnf fof_system_term ?head (bnf fof_arguments ?arguments)))) (body))

    ; ----------------------------------------------------------------- TFF
    (rule tff (head (metta-equation (tptp-canonical:tff ?value) (tptp-canonical:tff-form (tptp-canonical:shape-key (tptp-canonical:formula-heads) ?value) (langdef:value-view ?value) ?value))) (body))
    (rule apply-tff (head (metta-equation (tptp-canonical:apply-fn tff ?value) (tptp-canonical:tff ?value))) (body))
    (rule tff-quantified
      (head (metta-equation (tptp-canonical:tff-form quantifier (LangDef:ExpressionValue (LCons ?q (LCons ?variables (LCons ?body LNil)))) ?__value)
                            (bnf tff_quantified_formula (tptp-canonical:literal (tptp-canonical:quantifier-literals) ?q) (bnf tff_variable_list (tptp-canonical:each tff-variable ?variables)) (tptp-canonical:tff ?body))))
      (body))
    (rule tff-hash
      (head (metta-equation (tptp-canonical:tff-form hash (LangDef:ExpressionValue (LCons ?q (LCons ?variables (LCons ?body LNil)))) ?__value)
                            (bnf tff_quantified_formula "#" (bnf tff_variable_list (tptp-canonical:each tff-variable ?variables)) (tptp-canonical:tff ?body))))
      (body))
    (rule tff-unary
      (head (metta-equation (tptp-canonical:tff-form prefix (LangDef:ExpressionValue (LCons ?c (LCons ?operand LNil))) ?__value)
                            (bnf tff_prefix_unary (tptp-canonical:literal (tptp-canonical:symbol-literals) ?c) (tptp-canonical:tff ?operand))))
      (body))
    (rule tff-nonassoc
      (head (metta-equation (tptp-canonical:tff-form nonassoc (LangDef:ExpressionValue (LCons ?c (LCons ?a (LCons ?b LNil)))) ?__value)
                            (bnf tff_binary_nonassoc (tptp-canonical:tff ?a) (tptp-canonical:literal (tptp-canonical:symbol-literals) ?c) (tptp-canonical:tff ?b))))
      (body))
    (rule tff-or (head (metta-equation (tptp-canonical:tff-form or (LangDef:ExpressionValue (LCons ?c ?items)) ?__value) (tptp-canonical:chain tff_or_formula/0 tff_or_formula/1 (tptp-canonical:map tff ?items)))) (body))
    (rule tff-and (head (metta-equation (tptp-canonical:tff-form and (LangDef:ExpressionValue (LCons ?c ?items)) ?__value) (tptp-canonical:chain tff_and_formula/0 tff_and_formula/1 (tptp-canonical:map tff ?items)))) (body))
    (rule tff-equal
      (head (metta-equation (tptp-canonical:tff-form equal (LangDef:ExpressionValue (LCons ?c (LCons ?a (LCons ?b LNil)))) ?__value)
                            (bnf tff_defined_infix (tptp-canonical:tff ?a) "=" (tptp-canonical:tff ?b))))
      (body))
    (rule tff-unequal
      (head (metta-equation (tptp-canonical:tff-form unequal (LangDef:ExpressionValue (LCons ?c (LCons ?a (LCons ?b LNil)))) ?__value)
                            (bnf tff_infix_unary (tptp-canonical:tff ?a) "!=" (tptp-canonical:tff ?b))))
      (body))
    (rule tff-definition
      (head (metta-equation (tptp-canonical:tff-form definition (LangDef:ExpressionValue (LCons ?c (LCons ?a (LCons ?b LNil)))) ?__value)
                            (bnf txf_definition (tptp-canonical:tff ?a) "==" (tptp-canonical:tff ?b))))
      (body))
    (rule tff-sequent
      (head (metta-equation (tptp-canonical:tff-form sequent (LangDef:ExpressionValue (LCons ?c (LCons ?a (LCons ?b LNil)))) ?__value)
                            (bnf txf_sequent (tptp-canonical:tff ?a) "-->" (tptp-canonical:tff ?b))))
      (body))
    (rule tff-typing
      (head (metta-equation (tptp-canonical:tff-form typing (LangDef:ExpressionValue (LCons ?c (LCons ?atom (LCons ?type LNil)))) ?__value)
                            (bnf tff_atom_typing/0 (tptp-canonical:leaf ?atom) (tptp-canonical:tff-type ?type))))
      (body))
    (rule tff-subtype
      (head (metta-equation (tptp-canonical:tff-form subtype (LangDef:ExpressionValue (LCons ?c (LCons ?a (LCons ?b LNil)))) ?__value)
                            (bnf tff_subtype (tptp-canonical:leaf ?a) "<<" (tptp-canonical:leaf ?b))))
      (body))
    (rule tff-let
      (head (metta-equation (tptp-canonical:tff-form let (LangDef:ExpressionValue (LCons ?h (LCons ?typings (LCons ?definitions (LCons ?body LNil))))) ?__value)
                            (bnf txf_let (bnf txf_let_types (bnf tff_atom_typing_list (tptp-canonical:each tff-typing ?typings)))
                                         (bnf txf_let_defns (bnf txf_let_defn_list (tptp-canonical:each txf-definition ?definitions)))
                                         (tptp-canonical:tff ?body))))
      (body))
    (rule tff-typing-item
      (head (metta-equation (tptp-canonical:apply-fn tff-typing (: ?atom ?type))
                            (bnf tff_atom_typing/0 (tptp-canonical:leaf ?atom) (tptp-canonical:tff-type ?type))))
      (body))
    (rule txf-definition-item
      (head (metta-equation (tptp-canonical:apply-fn txf-definition (≔ ?lhs ?rhs))
                            (bnf txf_let_defn (tptp-canonical:tff ?lhs) ":=" (tptp-canonical:tff ?rhs))))
      (body))
    (rule tff-tuple-empty (head (metta-equation (tptp-canonical:tff-form tuple (LangDef:ListValue LNil) ?h) "[]")) (body))
    (rule tff-tuple
      (head (metta-equation (tptp-canonical:tff-form tuple (LangDef:ListValue (LCons ?first ?rest)) ?h)
                            (bnf txf_tuple (tptp-canonical:tff-arguments (LCons ?first ?rest)))))
      (body))
    (rule tff-arguments (head (metta-equation (tptp-canonical:tff-arguments ?arguments) (tptp-canonical:items tff_arguments tff_arguments*1 tff comma-tff ?arguments))) (body))
    (rule comma-tff (head (metta-equation (tptp-canonical:apply-fn comma-tff ?value) (bnf comma_tff_term (tptp-canonical:tff ?value)))) (body))
    (rule tff-leaf (head (metta-equation (tptp-canonical:tff-form leaf ?__view ?head) (tptp-canonical:leaf (tptp-canonical:rebuild ?head LNil)))) (body))
    (rule tff-apply (head (metta-equation (tptp-canonical:tff-form apply (LangDef:ExpressionValue (LCons ?head ?arguments)) ?__value) (tptp-canonical:tff-apply (tptp-canonical:leaf ?head) ?arguments))) (body))
    (rule tff-apply-token (head (metta-equation (tptp-canonical:tff-apply (bnf ?kind ?text) ?arguments) (tptp-canonical:tff-apply-as ?kind (bnf ?kind ?text) (tptp-canonical:tff-arguments ?arguments)))) (body))
    (rule tff-plain (head (metta-equation (tptp-canonical:tff-apply-as lower_word ?head ?arguments) (bnf tff_plain_atomic ?head ?arguments))) (body))
    (rule tff-plain-quoted (head (metta-equation (tptp-canonical:tff-apply-as single_quoted ?head ?arguments) (bnf tff_plain_atomic ?head ?arguments))) (body))
    (rule tff-defined-apply (head (metta-equation (tptp-canonical:tff-apply-as dollar_word ?head ?arguments) (bnf tff_defined_plain ?head ?arguments))) (body))
    (rule tff-system-apply (head (metta-equation (tptp-canonical:tff-apply-as dollar_dollar_word ?head ?arguments) (bnf tff_system_atomic ?head ?arguments))) (body))
    (rule tff-long-connective
      (head (metta-equation (tptp-canonical:tff-form long-connective (LangDef:ExpressionValue (LCons ?head ?arguments)) ?__value)
                            (bnf nxf_atom (tptp-canonical:long-connective nxf ?head) (tptp-canonical:tff-arguments ?arguments))))
      (body))
    (rule tff-variable (head (metta-equation (tptp-canonical:apply-fn tff-variable ?variable) (tptp-canonical:tff-variable-form (tptp-canonical:shape-key (tptp-canonical:formula-heads) ?variable) (langdef:value-view ?variable) ?variable))) (body))
    (rule tff-typed-variable
      (head (metta-equation (tptp-canonical:tff-variable-form typing (LangDef:ExpressionValue (LCons ?c (LCons ?variable (LCons ?type LNil)))) ?__value)
                            (bnf tff_typed_variable (tptp-canonical:leaf ?variable) (tptp-canonical:tff-type ?type))))
      (body))
    (rule tff-untyped-variable (head (metta-equation (tptp-canonical:tff-variable-form leaf ?__view ?head) (tptp-canonical:leaf (tptp-canonical:rebuild ?head LNil)))) (body))
    ; Types.
    (rule tff-type (head (metta-equation (tptp-canonical:tff-type ?value) (tptp-canonical:tff-type-form (tptp-canonical:shape-key (tptp-canonical:formula-heads) ?value) (langdef:value-view ?value) ?value))) (body))
    (rule apply-tff-type (head (metta-equation (tptp-canonical:apply-fn tff-type ?value) (tptp-canonical:tff-type ?value))) (body))
    (rule tff-mapping
      (head (metta-equation (tptp-canonical:tff-type-form mapping (LangDef:ExpressionValue (LCons ?c (LCons ?domain (LCons ?range LNil)))) ?__value)
                            (bnf tff_mapping_type (tptp-canonical:tff-type ?domain) (tptp-canonical:tff-type ?range))))
      (body))
    (rule tff-product (head (metta-equation (tptp-canonical:tff-type-form product (LangDef:ExpressionValue (LCons ?c ?items)) ?__value) (tptp-canonical:chain tff_xprod_type/0 tff_xprod_type/1 (tptp-canonical:map tff-type ?items)))) (body))
    (rule tff-type-quantified
      (head (metta-equation (tptp-canonical:tff-type-form quantifier (LangDef:ExpressionValue (LCons ?q (LCons ?variables (LCons ?body LNil)))) ?__value)
                            (bnf tf1_quantified_type (tptp-canonical:literal (tptp-canonical:quantifier-literals) ?q) (bnf tff_variable_list (tptp-canonical:each tff-variable ?variables)) (tptp-canonical:tff-type ?body))))
      (body))
    (rule tff-type-tuple (head (metta-equation (tptp-canonical:tff-type-form tuple (LangDef:ListValue ?items) ?c) (bnf txf_tuple_type (bnf tff_type_list (tptp-compact:list-value (tptp-canonical:map tff-type ?items)))))) (body))
    (rule tff-type-leaf (head (metta-equation (tptp-canonical:tff-type-form leaf ?__view ?head) (tptp-canonical:leaf (tptp-canonical:rebuild ?head LNil)))) (body))
    (rule tff-type-apply
      (head (metta-equation (tptp-canonical:tff-type-form apply (LangDef:ExpressionValue (LCons ?head ?arguments)) ?__value)
                            (bnf tff_atomic_type/3 (tptp-canonical:leaf ?head) (bnf tff_type_arguments (tptp-compact:list-value (tptp-canonical:map tff-type ?arguments))))))
      (body))

    ; ----------------------------------------------------------------- THF
    (rule thf (head (metta-equation (tptp-canonical:thf ?value) (tptp-canonical:thf-form (tptp-canonical:shape-key (tptp-canonical:formula-heads) ?value) (langdef:value-view ?value) ?value))) (body))
    (rule apply-thf (head (metta-equation (tptp-canonical:apply-fn thf ?value) (tptp-canonical:thf ?value))) (body))
    (rule thf-quantified
      (head (metta-equation (tptp-canonical:thf-form quantifier (LangDef:ExpressionValue (LCons ?q (LCons ?variables (LCons ?body LNil)))) ?__value)
                            (bnf thf_quantified_formula
                              (bnf thf_quantification (tptp-canonical:literal (tptp-canonical:quantifier-literals) ?q) (bnf thf_variable_list (tptp-canonical:each thf-variable ?variables)))
                              (tptp-canonical:thf ?body))))
      (body))
    (rule thf-hash
      (head (metta-equation (tptp-canonical:thf-form hash (LangDef:ExpressionValue (LCons ?q (LCons ?variables (LCons ?body LNil)))) ?__value)
                            (bnf thf_quantified_formula (bnf thf_quantification "#" (bnf thf_variable_list (tptp-canonical:each thf-variable ?variables))) (tptp-canonical:thf ?body))))
      (body))
    (rule thf-unary
      (head (metta-equation (tptp-canonical:thf-form prefix (LangDef:ExpressionValue (LCons ?c (LCons ?operand LNil))) ?__value)
                            (bnf thf_prefix_unary (tptp-canonical:literal (tptp-canonical:symbol-literals) ?c) (tptp-canonical:thf ?operand))))
      (body))
    (rule thf-nonassoc
      (head (metta-equation (tptp-canonical:thf-form nonassoc (LangDef:ExpressionValue (LCons ?c (LCons ?a (LCons ?b LNil)))) ?__value)
                            (bnf thf_binary_nonassoc (tptp-canonical:thf ?a) (tptp-canonical:literal (tptp-canonical:symbol-literals) ?c) (tptp-canonical:thf ?b))))
      (body))
    (rule thf-or (head (metta-equation (tptp-canonical:thf-form or (LangDef:ExpressionValue (LCons ?c ?items)) ?__value) (tptp-canonical:chain thf_or_formula/0 thf_or_formula/1 (tptp-canonical:map thf ?items)))) (body))
    (rule thf-and (head (metta-equation (tptp-canonical:thf-form and (LangDef:ExpressionValue (LCons ?c ?items)) ?__value) (tptp-canonical:chain thf_and_formula/0 thf_and_formula/1 (tptp-canonical:map thf ?items)))) (body))
    (rule thf-equal
      (head (metta-equation (tptp-canonical:thf-form equal (LangDef:ExpressionValue (LCons ?c (LCons ?a (LCons ?b LNil)))) ?__value)
                            (bnf thf_defined_infix (tptp-canonical:thf ?a) "=" (tptp-canonical:thf ?b))))
      (body))
    (rule thf-unequal
      (head (metta-equation (tptp-canonical:thf-form unequal (LangDef:ExpressionValue (LCons ?c (LCons ?a (LCons ?b LNil)))) ?__value)
                            (bnf thf_infix_unary (tptp-canonical:thf ?a) "!=" (tptp-canonical:thf ?b))))
      (body))
    (rule thf-definition
      (head (metta-equation (tptp-canonical:thf-form definition (LangDef:ExpressionValue (LCons ?c (LCons ?a (LCons ?b LNil)))) ?__value)
                            (bnf thf_definition (tptp-canonical:thf ?a) "==" (tptp-canonical:thf ?b))))
      (body))
    (rule thf-sequent
      (head (metta-equation (tptp-canonical:thf-form sequent (LangDef:ExpressionValue (LCons ?c (LCons ?a (LCons ?b LNil)))) ?__value)
                            (bnf thf_sequent (tptp-canonical:thf ?a) "-->" (tptp-canonical:thf ?b))))
      (body))
    (rule thf-typing
      (head (metta-equation (tptp-canonical:thf-form typing (LangDef:ExpressionValue (LCons ?c (LCons ?atom (LCons ?type LNil)))) ?__value)
                            (bnf thf_atom_typing/0 (tptp-canonical:leaf ?atom) (tptp-canonical:thf ?type))))
      (body))
    (rule thf-subtype
      (head (metta-equation (tptp-canonical:thf-form subtype (LangDef:ExpressionValue (LCons ?c (LCons ?a (LCons ?b LNil)))) ?__value)
                            (bnf thf_subtype (tptp-canonical:leaf ?a) "<<" (tptp-canonical:leaf ?b))))
      (body))
    ; a > b > c nests to the right.
    (rule thf-mapping
      (head (metta-equation (tptp-canonical:thf-form mapping (LangDef:ExpressionValue (LCons ?c (LCons ?domain (LCons ?range LNil)))) ?__value)
                            (tptp-canonical:thf-mapping (langdef:ground-equal (tptp-canonical:value-key (tptp-canonical:formula-heads) ?range) mapping) ?domain ?range)))
      (body))
    (rule thf-mapping-nested (head (metta-equation (tptp-canonical:thf-mapping LangDef:SameValue ?domain ?range) (bnf thf_mapping_type/1 (tptp-canonical:thf ?domain) (tptp-canonical:thf ?range)))) (body))
    (rule thf-mapping-last (head (metta-equation (tptp-canonical:thf-mapping LangDef:DifferentValue ?domain ?range) (bnf thf_mapping_type/0 (tptp-canonical:thf ?domain) (tptp-canonical:thf ?range)))) (body))
    (rule thf-product (head (metta-equation (tptp-canonical:thf-form product (LangDef:ExpressionValue (LCons ?c ?items)) ?__value) (tptp-canonical:chain thf_xprod_type/0 thf_xprod_type/1 (tptp-canonical:map thf ?items)))) (body))
    (rule thf-union (head (metta-equation (tptp-canonical:thf-form union (LangDef:ExpressionValue (LCons ?c ?items)) ?__value) (tptp-canonical:chain thf_union_type/0 thf_union_type/1 (tptp-canonical:map thf ?items)))) (body))
    (rule thf-let
      (head (metta-equation (tptp-canonical:thf-form let (LangDef:ExpressionValue (LCons ?h (LCons ?typings (LCons ?definitions (LCons ?body LNil))))) ?__value)
                            (bnf thf_let (bnf thf_let_types (bnf thf_atom_typing_list (tptp-canonical:each thf-typing ?typings)))
                                         (bnf thf_let_defns (bnf thf_let_defn_list (tptp-canonical:each thf-definition ?definitions)))
                                         (tptp-canonical:thf ?body))))
      (body))
    (rule thf-typing-item
      (head (metta-equation (tptp-canonical:apply-fn thf-typing (: ?atom ?type))
                            (bnf thf_atom_typing/0 (tptp-canonical:leaf ?atom) (tptp-canonical:thf ?type))))
      (body))
    (rule thf-definition-item
      (head (metta-equation (tptp-canonical:apply-fn thf-definition (≔ ?lhs ?rhs))
                            (bnf thf_let_defn (tptp-canonical:thf ?lhs) ":=" (tptp-canonical:thf ?rhs))))
      (body))
    (rule thf-tuple-empty (head (metta-equation (tptp-canonical:thf-form tuple (LangDef:ListValue LNil) ?h) "[]")) (body))
    (rule thf-tuple
      (head (metta-equation (tptp-canonical:thf-form tuple (LangDef:ListValue (LCons ?first ?rest)) ?h)
                            (bnf thf_tuple (tptp-canonical:items thf_formula_list thf_formula_list*1 thf comma-thf (LCons ?first ?rest)))))
      (body))
    (rule comma-thf (head (metta-equation (tptp-canonical:apply-fn comma-thf ?value) (bnf comma_thf_logic_formula (tptp-canonical:thf ?value)))) (body))
    (rule thf-leaf (head (metta-equation (tptp-canonical:thf-form leaf ?__view ?head) (tptp-canonical:leaf (tptp-canonical:rebuild ?head LNil)))) (body))
    ; An application is the left-nested chain of @ over its arguments.
    (rule thf-apply
      (head (metta-equation (tptp-canonical:thf-form apply (LangDef:ExpressionValue (LCons ?head ?arguments)) ?__value)
                            (tptp-canonical:applied (tptp-canonical:thf ?head) (tptp-canonical:map thf ?arguments))))
      (body))
    (rule thf-long-connective
      (head (metta-equation (tptp-canonical:thf-form long-connective (LangDef:ExpressionValue (LCons ?head ?arguments)) ?__value)
                            (tptp-canonical:applied (tptp-canonical:long-connective nhf ?head) (tptp-canonical:map thf ?arguments))))
      (body))
    (rule applied
      (head (metta-equation (tptp-canonical:applied ?head (LCons ?argument ?rest))
                            (tptp-canonical:chain-rest thf_apply_formula/1 (bnf thf_apply_formula/0 ?head ?argument) ?rest)))
      (body))
    (rule thf-variable (head (metta-equation (tptp-canonical:apply-fn thf-variable ?variable) (tptp-canonical:thf-variable-form (tptp-canonical:shape-key (tptp-canonical:formula-heads) ?variable) (langdef:value-view ?variable) ?variable))) (body))
    (rule thf-typed-variable
      (head (metta-equation (tptp-canonical:thf-variable-form typing (LangDef:ExpressionValue (LCons ?c (LCons ?variable (LCons ?type LNil)))) ?__value)
                            (bnf thf_typed_variable (tptp-canonical:leaf ?variable) (tptp-canonical:thf ?type))))
      (body))
    ; Long connectives, {$c} and {$c(P...)}, for NHF and NXF.
    (rule long-connective (head (metta-equation (tptp-canonical:long-connective ?family ?connective) (tptp-canonical:long-connective-list ?family (langdef:expression->list ?connective)))) (body))
    (rule nhf-bare (head (metta-equation (tptp-canonical:long-connective-list nhf (LCons ?name LNil)) (bnf nhf_long_connective/0 (tptp-canonical:leaf ?name)))) (body))
    (rule nhf-parameters
      (head (metta-equation (tptp-canonical:long-connective-list nhf (LCons ?name (LCons ?parameter ?rest)))
                            (bnf nhf_long_connective/1 (tptp-canonical:leaf ?name) (bnf nhf_parameter_list (tptp-compact:list-value (tptp-canonical:map nhf-parameter (LCons ?parameter ?rest)))))))
      (body))
    (rule nxf-bare (head (metta-equation (tptp-canonical:long-connective-list nxf (LCons ?name LNil)) (bnf nxf_long_connective/0 (tptp-canonical:leaf ?name)))) (body))
    (rule nxf-parameters
      (head (metta-equation (tptp-canonical:long-connective-list nxf (LCons ?name (LCons ?parameter ?rest)))
                            (bnf nxf_long_connective/1 (tptp-canonical:leaf ?name) (bnf nxf_parameter_list (tptp-compact:list-value (tptp-canonical:map nxf-parameter (LCons ?parameter ?rest)))))))
      (body))
    (rule nhf-parameter (head (metta-equation (tptp-canonical:apply-fn nhf-parameter ?parameter) (tptp-canonical:parameter thf (tptp-canonical:shape-key (tptp-canonical:formula-heads) ?parameter) (langdef:value-view ?parameter) ?parameter ?parameter))) (body))
    (rule nxf-parameter (head (metta-equation (tptp-canonical:apply-fn nxf-parameter ?parameter) (tptp-canonical:parameter tff (tptp-canonical:shape-key (tptp-canonical:formula-heads) ?parameter) (langdef:value-view ?parameter) ?parameter ?parameter))) (body))
    (rule parameter-index (head (metta-equation (tptp-canonical:parameter ?family hash (LangDef:ExpressionValue (LCons ?h (LCons ?term LNil))) ?__value ?parameter) (bnf ntf_index (tptp-canonical:tff ?term)))) (body))
    (rule parameter-thf-definition (head (metta-equation (tptp-canonical:parameter thf definition (LangDef:ExpressionValue (LCons ?h ?arguments)) ?__value ?parameter) (tptp-canonical:thf (langdef:list->expression (LCons ?h ?arguments))))) (body))
    (rule parameter-tff-definition (head (metta-equation (tptp-canonical:parameter tff definition (LangDef:ExpressionValue (LCons ?h ?arguments)) ?__value ?parameter) (tptp-canonical:tff (langdef:list->expression (LCons ?h ?arguments))))) (body))

    ; ------------------------------------------------------------ CNF, TCF
    (rule inverse-clause (head (metta-equation (tptp-canonical:clause ?clause) (tptp-canonical:clause-list (langdef:expression->list ?clause)))) (body))
    (rule clause-literals (head (metta-equation (tptp-canonical:clause-list (LCons ∨ ?literals)) (tptp-canonical:disjunction (tptp-canonical:map literal ?literals)))) (body))
    (rule literal-item
      (head (metta-equation (tptp-canonical:apply-fn literal ?literal)
                            (tptp-canonical:literal-at (langdef:ground-equal (tptp-canonical:value-key (tptp-canonical:formula-heads) ?literal) prefix) ?literal)))
      (body))
    (rule literal-negative (head (metta-equation (tptp-canonical:literal-at LangDef:SameValue (¬ ?atom)) (bnf cnf_literal/1 (tptp-canonical:fof ?atom)))) (body))
    (rule literal-positive (head (metta-equation (tptp-canonical:literal-at LangDef:DifferentValue ?literal) (tptp-canonical:fof ?literal))) (body))
    (rule inverse-tcf (head (metta-equation (tptp-canonical:tcf ?value) (tptp-canonical:tcf-form (tptp-canonical:shape-key (tptp-canonical:formula-heads) ?value) (langdef:value-view ?value) ?value ?value))) (body))
    (rule tcf-quantified
      (head (metta-equation (tptp-canonical:tcf-form quantifier (LangDef:ExpressionValue (LCons ?q (LCons ?variables (LCons ?body LNil)))) ?__value ?value)
                            (bnf tcf_quantified_formula (bnf tff_variable_list (tptp-canonical:each tff-variable ?variables)) (tptp-canonical:tcf ?body))))
      (body))
    (rule inverse-tcf-clause (head (metta-equation (tptp-canonical:tcf-form or (LangDef:ExpressionValue (LCons ?c ?literals)) ?__value ?value) (tptp-canonical:clause (langdef:list->expression (LCons ?c ?literals))))) (body))
    (rule tcf-typing (head (metta-equation (tptp-canonical:tcf-form typing (LangDef:ExpressionValue (LCons ?c ?arguments)) ?__value ?value) (tptp-canonical:tff (langdef:list->expression (LCons ?c ?arguments))))) (body))

    ; ------------------------------------------------------------- sources
    (rule source (head (metta-equation (tptp-canonical:source ?value) (tptp-canonical:source-form (tptp-canonical:shape-key (tptp-canonical:source-heads) ?value) (langdef:value-view ?value) ?value ?value))) (body))
    (rule apply-source (head (metta-equation (tptp-canonical:apply-fn source ?value) (tptp-canonical:source ?value))) (body))
    (rule source-inference
      (head (metta-equation (tptp-canonical:source-form inference (LangDef:ExpressionValue (LCons ?h (LCons ?rule (LCons ?info (LCons ?parents LNil))))) ?__value ?value)
                            (bnf inference_record (tptp-canonical:leaf ?rule) (tptp-canonical:useful-info ?info) (tptp-canonical:parents ?parents))))
      (body))
    (rule source-creator
      (head (metta-equation (tptp-canonical:source-form creator (LangDef:ExpressionValue (LCons ?h (LCons ?name (LCons ?info (LCons ?parents LNil))))) ?__value ?value)
                            (bnf creator_source (tptp-canonical:leaf ?name) (tptp-canonical:useful-info ?info) (tptp-canonical:parents ?parents))))
      (body))
    (rule source-introduced (head (metta-equation (tptp-canonical:source-form introduced (LangDef:ExpressionValue (LCons ?h ?arguments)) ?__value ?value) (tptp-canonical:introduced ?arguments))) (body))
    (rule source-file (head (metta-equation (tptp-canonical:source-form file (LangDef:ExpressionValue (LCons ?h (LCons ?file LNil))) ?__value ?value) (bnf file_source (tptp-canonical:file-name ?file) (bnf nothing)))) (body))
    (rule source-file-name
      (head (metta-equation (tptp-canonical:source-form file (LangDef:ExpressionValue (LCons ?h (LCons ?file (LCons ?name LNil)))) ?__value ?value)
                            (bnf file_source (tptp-canonical:file-name ?file) (bnf file_info (tptp-canonical:leaf ?name)))))
      (body))
    (rule source-theory (head (metta-equation (tptp-canonical:source-form theory (LangDef:ExpressionValue (LCons ?h (LCons ?name LNil))) ?__value ?value) (bnf theory (tptp-canonical:leaf ?name) (bnf nothing)))) (body))
    (rule source-theory-info
      (head (metta-equation (tptp-canonical:source-form theory (LangDef:ExpressionValue (LCons ?h (LCons ?name (LCons ?info LNil)))) ?__value ?value)
                            (bnf theory (tptp-canonical:leaf ?name) (bnf optional_info (tptp-canonical:useful-info ?info)))))
      (body))
    (rule source-list (head (metta-equation (tptp-canonical:source-form tuple (LangDef:ListValue ?sources) ?h ?value) (bnf source (bnf sources (tptp-compact:list-value (tptp-canonical:map source ?sources)))))) (body))
    ; At a source position the bare symbol unknown is the unknown source.
    (rule source-leaf
      (head (metta-equation (tptp-canonical:source-form leaf ?__view ?h ?value)
                            (tptp-canonical:source-leaf (langdef:ground-equal ?value unknown) ?value)))
      (body))
    (rule source-unknown (head (metta-equation (tptp-canonical:source-leaf LangDef:SameValue ?value) "unknown")) (body))
    (rule source-name (head (metta-equation (tptp-canonical:source-leaf LangDef:DifferentValue ?value) (tptp-canonical:leaf ?value))) (body))
    (rule useful-info (head (metta-equation (tptp-canonical:useful-info ?info) (tptp-canonical:bracketed-general (langdef:expression->list ?info)))) (body))
    (rule bracketed-empty (head (metta-equation (tptp-canonical:bracketed-general LNil) "[]")) (body))
    (rule bracketed-terms
      (head (metta-equation (tptp-canonical:bracketed-general (LCons ?first ?rest))
                            (bnf general_list (tptp-canonical:items general_terms general_terms*1 general comma-general (LCons ?first ?rest)))))
      (body))
    (rule parents (head (metta-equation (tptp-canonical:parents ?parents) (tptp-canonical:parent-list (langdef:expression->list ?parents)))) (body))
    (rule inverse-parents-empty (head (metta-equation (tptp-canonical:parent-list LNil) "[]")) (body))
    (rule parents-some
      (head (metta-equation (tptp-canonical:parent-list (LCons ?first ?rest))
                            (bnf parents (tptp-canonical:items parent_list parent_list*1 parent comma-parent (LCons ?first ?rest)))))
      (body))
    (rule comma-parent (head (metta-equation (tptp-canonical:apply-fn comma-parent ?value) (bnf comma_parent_info (tptp-canonical:parent ?value)))) (body))
    (rule apply-parent (head (metta-equation (tptp-canonical:apply-fn parent ?value) (tptp-canonical:parent ?value))) (body))
    (rule parent
      (head (metta-equation (tptp-canonical:parent ?value)
                            (tptp-canonical:parent-at (langdef:ground-equal (tptp-canonical:value-key (tptp-canonical:general-heads) ?value) typing) ?value)))
      (body))
    (rule inverse-parent-detailed
      (head (metta-equation (tptp-canonical:parent-at LangDef:SameValue (: ?source ?details))
                            (bnf parent_info (tptp-canonical:source ?source) (bnf parent_details (tptp-canonical:general ?details)))))
      (body))
    (rule inverse-parent-plain (head (metta-equation (tptp-canonical:parent-at LangDef:DifferentValue ?value) (bnf parent_info (tptp-canonical:source ?value) (bnf nothing)))) (body))

    ; ------------------------------------------------------- general terms
    (rule general (head (metta-equation (tptp-canonical:general ?value) (tptp-canonical:general-form (tptp-canonical:shape-key (tptp-canonical:general-heads) ?value) (langdef:value-view ?value) ?value ?value))) (body))
    (rule apply-general (head (metta-equation (tptp-canonical:apply-fn general ?value) (tptp-canonical:general ?value))) (body))
    (rule comma-general (head (metta-equation (tptp-canonical:apply-fn comma-general ?value) (bnf comma_general_term (tptp-canonical:general ?value)))) (body))
    (rule general-typed
      (head (metta-equation (tptp-canonical:general-form typing (LangDef:ExpressionValue (LCons ?h (LCons ?data (LCons ?term LNil)))) ?__value ?value)
                            (bnf general_term (tptp-canonical:general ?data) (tptp-canonical:general ?term))))
      (body))
    (rule general-list (head (metta-equation (tptp-canonical:general-form tuple (LangDef:ListValue ?items) ?h ?value) (tptp-canonical:bracketed-general ?items))) (body))
    (rule general-leaf (head (metta-equation (tptp-canonical:general-form leaf ?__view ?h ?value) (tptp-canonical:leaf ?value))) (body))
    (rule general-function
      (head (metta-equation (tptp-canonical:general-form apply (LangDef:ExpressionValue (LCons ?h ?arguments)) ?__value ?value)
                            (bnf general_function (tptp-canonical:leaf ?h) (tptp-canonical:items general_terms general_terms*1 general comma-general ?arguments))))
      (body))
    (rule general-thf (head (metta-equation (tptp-canonical:general-form data-thf (LangDef:ExpressionValue (LCons ?h (LCons ?formula LNil))) ?__value ?value) (bnf formula_data/0 (tptp-canonical:thf ?formula)))) (body))
    (rule general-tff (head (metta-equation (tptp-canonical:general-form data-tff (LangDef:ExpressionValue (LCons ?h (LCons ?formula LNil))) ?__value ?value) (bnf formula_data/1 (tptp-canonical:tff ?formula)))) (body))
    (rule general-fof (head (metta-equation (tptp-canonical:general-form data-fof (LangDef:ExpressionValue (LCons ?h (LCons ?formula LNil))) ?__value ?value) (bnf formula_data/2 (tptp-canonical:fof ?formula)))) (body))
    (rule general-cnf (head (metta-equation (tptp-canonical:general-form data-cnf (LangDef:ExpressionValue (LCons ?h (LCons ?formula LNil))) ?__value ?value) (bnf formula_data/3 (tptp-canonical:clause ?formula)))) (body))
    (rule general-fot (head (metta-equation (tptp-canonical:general-form data-fot (LangDef:ExpressionValue (LCons ?h (LCons ?term LNil))) ?__value ?value) (bnf formula_data/4 (tptp-canonical:fof ?term)))) (body))
  ))
