; The first-order resolution input of compact TPTP clauses.
;
; A source document read under the tagged policy gives its clauses: each keeps
; its source occurrence (the document's digest and its input index), its name
; and role, and its literals.  A literal is its atom or (¬ ATOM); (≉ A B) is
; the negative literal of (≈ A B).  Variables are scoped by their occurrence,
; so the clauses of distinct inputs never share one.  A word names a lower
; symbol when its text is a TPTP lower word, 'cat' and cat alike, and a quoted
; symbol otherwise.  An input that is not a clause is refused by name: a
; formula must be clausified first and an include expanded.
;
; A document with its includes expanded gives the clauses of every file it
; reaches.  An included clause's occurrence is its own file's digest and index
; and the occurrence of the include directive that reached it, so a file
; included twice gives two inputs with separate variable scopes, and the
; include chain of every input can be checked against the source documents.
; One record projects alone at a given occurrence, which is how such a check
; recomputes an input clause.
;
; A formula reaches the resolution input through the verified clausification
; stages.  Its compact form is first read as a named first-order formula whose
; symbols carry their compact texts, together with its polarity (a conjecture
; is negated) and the distinct names its binders bind.  The clausal form those
; stages return is then read back as clauses at clausal occurrences: the
; formula's occurrence and the clause's position.  An introduced Skolem
; function or definitional predicate keeps its introduction index under its
; own symbol kind.  A clause with a true literal is omitted and a false literal
; is left out of its clause; both keep satisfiability.  A conjecture's clauses
; have the role negated_conjecture.
(gslt-presentation-v1 TptpCompactToResolutionInputV1
  (signature
    (operator metta-equation 2) (operator + 2)
    (operator LNil 0) (operator LCons 2)
    (operator TPTP:SourceDocument 5)
    (operator TPTP:ExpandedDocumentV1 3) (operator TPTP:ExpandedInputV1 5)
    (operator TPTP:SourceOccurrenceV1 4)
    (operator TPTP:IncludePathRootV1 1) (operator TPTP:IncludePathStepV1 3)
    (operator tptp-ci-v1:expanded-inputs-nil 0)
    (operator tptp-ci-v1:expanded-inputs-cons 2)
    (operator TPTP:ResolutionInputUnsupportedV1 1)
    (operator langdef:value-view 1) (operator langdef:ground-equal 2)
    (operator str:concat 2) (operator str:lower-word? 1)
    (operator LangDef:ListValue 1) (operator LangDef:ExpressionValue 1)
    (operator LangDef:SymbolValue 1) (operator LangDef:IntegerValue 1)
    (operator LangDef:SameValue 0) (operator LangDef:DifferentValue 0)
    (operator fo-resolution:source-digest 1) (operator fo-resolution:occurrence 2)
    (operator fo-resolution:included-occurrence 3)
    (operator fo-resolution:formula-name-atomic 1)
    (operator fo-resolution:formula-name-integer 1)
    (operator fo-resolution:role 1)
    (operator fo-resolution:symbol-lower 0) (operator fo-resolution:symbol-quoted 0)
    (operator fo-resolution:symbol-defined 0) (operator fo-resolution:symbol-system 0)
    (operator fo-resolution:symbol-name 2)
    (operator fo-resolution:variable-name 1) (operator fo-resolution:variable-id 2)
    (operator fo-resolution:number-integer 0) (operator fo-resolution:number-rational 0)
    (operator fo-resolution:number-real 0)
    (operator fo-resolution:term-variable 1) (operator fo-resolution:term-function 2)
    (operator fo-resolution:term-number 2) (operator fo-resolution:term-distinct 1)
    (operator fo-resolution:terms-nil 0) (operator fo-resolution:terms-cons 2)
    (operator fo-resolution:atom-predicate 2) (operator fo-resolution:atom-equality 2)
    (operator fo-resolution:literal-positive 1) (operator fo-resolution:literal-negative 1)
    (operator fo-resolution:literals-nil 0) (operator fo-resolution:literals-cons 2)
    (operator fo-resolution:clause 4)
    (operator fo-resolution:clauses-nil 0) (operator fo-resolution:clauses-cons 2)
    (operator fo-resolution:problem 2)
    (operator tptp-res-v1:problem 1) (operator tptp-res-v1:problem-of 2)
    (operator tptp-res-v1:ok 1) (operator tptp-res-v1:prepend 2)
    (operator tptp-res-v1:inputs 3) (operator tptp-res-v1:items 3)
    (operator tptp-res-v1:input 4) (operator tptp-res-v1:input-of 5)
    (operator tptp-res-v1:clause 4) (operator tptp-res-v1:disjunction 2)
    (operator tptp-res-v1:expanded-problem 2) (operator tptp-res-v1:expanded-inputs 1)
    (operator tptp-res-v1:expanded-input 3) (operator tptp-res-v1:expanded-input-of 5)
    (operator tptp-res-v1:occurrence-at 3)
    (operator tptp-res-v1:record-clause 2) (operator tptp-res-v1:record-clause-of 2)
    (operator tptp-res-v1:record-clause-at 4)
    (operator tptp-res-v1:literals 2) (operator tptp-res-v1:literal 2)
    (operator tptp-res-v1:literal-at 4)
    (operator tptp-res-v1:atom 2) (operator tptp-res-v1:atom-at 4)
    (operator tptp-res-v1:terms 2) (operator tptp-res-v1:term 2)
    (operator tptp-res-v1:term-of 3) (operator tptp-res-v1:term-at 4)
    (operator tptp-res-v1:leaf-symbol 1) (operator tptp-res-v1:symbol 2)
    (operator tptp-res-v1:head-kind 1) (operator tptp-res-v1:head-kind-of 2)
    (operator tptp-res-v1:head-kinds 0) (operator tptp-res-v1:pair 2)
    (operator tptp-res-v1:lookup 2) (operator tptp-res-v1:lookup-at 4)
    (operator tptp-res-v1:name 1) (operator tptp-res-v1:name-of 1)
    (operator tptp-res-v1:role 1) (operator tptp-res-v1:role-of 1)
    (operator tptp-res-v1:word-kind 1) (operator tptp-res-v1:word-kind-of 1)
    (operator tptp-res-v1:integer-text 1) (operator tptp-res-v1:integer-text-of 1)
    (operator tptp-res-v1:rational-lexeme 2)
    (operator not 0) (operator equal 0) (operator unequal 0)
    (operator variable 0) (operator leaf 0) (operator apply 0)
    (operator distinct-object 0) (operator rational 0) (operator real-number 0)
    (operator tptp-fof-named:name 1) (operator tptp-fof-named:term-variable 1)
    (operator tptp-fof-named:term-function 2)
    (operator tptp-fof-named:terms-nil 0) (operator tptp-fof-named:terms-cons 2)
    (operator tptp-fof-named:predicate 2) (operator tptp-fof-named:equal 2)
    (operator tptp-fof-named:verum 0) (operator tptp-fof-named:falsum 0)
    (operator tptp-fof-named:not 1) (operator tptp-fof-named:and 2)
    (operator tptp-fof-named:or 2) (operator tptp-fof-named:iff 2)
    (operator tptp-fof-named:implies 2) (operator tptp-fof-named:reverse-implies 2)
    (operator tptp-fof-named:xor 2) (operator tptp-fof-named:nor 2)
    (operator tptp-fof-named:nand 2)
    (operator tptp-fof-named:all 2) (operator tptp-fof-named:ex 2)
    (operator tptp-fof-symbol:function-plain 1) (operator tptp-fof-symbol:function-defined 1)
    (operator tptp-fof-symbol:function-system 1) (operator tptp-fof-symbol:function-integer 1)
    (operator tptp-fof-symbol:function-rational 1) (operator tptp-fof-symbol:function-real 1)
    (operator tptp-fof-symbol:function-distinct-object 1)
    (operator tptp-fof-symbol:predicate-plain 1) (operator tptp-fof-symbol:predicate-defined 1)
    (operator tptp-fof-symbol:predicate-system 1)
    (operator tptp-fof-resolved:index-zero 0) (operator tptp-fof-resolved:index-succ 1)
    (operator tptp-fof-skolem:term-variable 1) (operator tptp-fof-skolem:term-original 2)
    (operator tptp-fof-skolem:term-generated 2)
    (operator tptp-fof-skolem:terms-nil 0) (operator tptp-fof-skolem:terms-cons 2)
    (operator tptp-fof-named:ref-original-positive 2) (operator tptp-fof-named:ref-original-negative 2)
    (operator tptp-fof-named:ref-equal 2) (operator tptp-fof-named:ref-not-equal 2)
    (operator tptp-fof-named:ref-defined-positive 2) (operator tptp-fof-named:ref-defined-negative 2)
    (operator tptp-fof-named:ref-verum 0) (operator tptp-fof-named:ref-falsum 0)
    (operator tptp-fof-cnf:clause-nil 0) (operator tptp-fof-cnf:clause-cons 2)
    (operator tptp-fof-cnf:clauses-nil 0) (operator tptp-fof-cnf:clauses-cons 2)
    (operator fo-resolution:clausal-occurrence 2)
    (operator fo-resolution:symbol-skolem 0) (operator fo-resolution:symbol-definition 0)
    (operator TPTP:ClausificationRequestV1 3)
    (operator TPTP:ClausificationInputUnsupportedV1 2)
    (operator tptp-fof-batch:positive 0) (operator tptp-fof-batch:negative 0)
    (operator tptp-res-v1:clausification-request 1)
    (operator tptp-res-v1:clausification-request-of 2)
    (operator tptp-res-v1:clausification-fof 4)
    (operator tptp-res-v1:clausification-polarity 1) (operator tptp-res-v1:polarity-at 2)
    (operator tptp-res-v1:role-polarities 0) (operator tptp-res-v1:request-with 4)
    (operator tptp-res-v1:named-formula 1) (operator tptp-res-v1:named-formula-of 2)
    (operator tptp-res-v1:named-at 3) (operator tptp-res-v1:named-truth 2)
    (operator tptp-res-v1:formula-kinds 0) (operator tptp-res-v1:formula-kind 1)
    (operator tptp-res-v1:formula-kind-of 2)
    (operator tptp-res-v1:named-and 1) (operator tptp-res-v1:named-or 1)
    (operator tptp-res-v1:named-all 2) (operator tptp-res-v1:named-ex 2)
    (operator tptp-res-v1:named-binders 3) (operator tptp-res-v1:binder-name 1)
    (operator tptp-res-v1:named-atom 2) (operator tptp-res-v1:predicate-head 1)
    (operator tptp-res-v1:predicate-head-of 1)
    (operator tptp-res-v1:named-terms 1) (operator tptp-res-v1:named-term 1)
    (operator tptp-res-v1:named-term-of 2) (operator tptp-res-v1:named-term-at 3)
    (operator tptp-res-v1:function-head 1) (operator tptp-res-v1:function-head-of 1)
    (operator tptp-res-v1:binder-names 2) (operator tptp-res-v1:binder-names-of 2)
    (operator tptp-res-v1:binder-names-at 4) (operator tptp-res-v1:binder-names-all 2)
    (operator tptp-res-v1:binder-names-list 2) (operator tptp-res-v1:add-name 2)
    (operator tptp-res-v1:add-name-at 3) (operator tptp-res-v1:member-name 2)
    (operator tptp-res-v1:member-name-at 3)
    (operator tptp-res-v1:clausal-form 5) (operator tptp-res-v1:clausal-clauses 5)
    (operator tptp-res-v1:clausal-clause 6) (operator tptp-res-v1:clause-literals 2)
    (operator tptp-res-v1:literal-prepend 2) (operator tptp-res-v1:reference 2)
    (operator tptp-res-v1:a-literal 1) (operator tptp-res-v1:true-reference 0)
    (operator tptp-res-v1:false-reference 0) (operator tptp-res-v1:tautology 0)
    (operator tptp-res-v1:items-of 1)
    (operator tptp-res-v1:binder-names-all-binders 2) (operator tptp-res-v1:join-names 2)
    (operator tptp-res-v1:clausal-role 2)
    (operator tptp-res-v1:skolem-terms 2) (operator tptp-res-v1:skolem-term 2)
    (operator tptp-res-v1:original-term 3) (operator tptp-res-v1:predicate-symbol 1)
    (operator tptp-res-v1:index-integer 1) (operator tptp-res-v1:index-text 1)
    (operator tptp-res-v1:variable-text 1)
    (operator tptp-res-v1:item 2) (operator tptp-res-v1:expanded-items 1)
    (operator tptp-res-v1:document-items 3) (operator tptp-res-v1:document-items-of 3)
    (operator universal 0) (operator existential 0) (operator conjunction 0)
    (operator disjunction 0) (operator implication 0) (operator reverse-implication 0)
    (operator equivalence 0) (operator exclusive-or 0) (operator joint-denial 0)
    (operator alternative-denial 0) (operator negation 0) (operator equality 0)
    (operator disequality 0) (operator proposition 0)
    (operator refused 0) (operator unsupported-role 0)
    (operator absent 0))
  (equations)
  (rewrites
    ; ------------------------------------------------------------ document
    (rule problem (head (metta-equation
      (tptp-res-v1:problem (TPTP:SourceDocument ?identity ?digest ?text ?records ?spans))
      (tptp-res-v1:problem-of ?digest
        (tptp-res-v1:inputs (langdef:value-view ?records) ?digest 0)))) (body))
    (rule problem-ok (head (metta-equation
      (tptp-res-v1:problem-of ?digest (tptp-res-v1:ok ?clauses))
      (fo-resolution:problem (fo-resolution:source-digest ?digest) ?clauses))) (body))
    (rule problem-refused (head (metta-equation
      (tptp-res-v1:problem-of ?digest (TPTP:ResolutionInputUnsupportedV1 ?family))
      (TPTP:ResolutionInputUnsupportedV1 ?family))) (body))
    (rule prepend-ok (head (metta-equation
      (tptp-res-v1:prepend ?clause (tptp-res-v1:ok ?clauses))
      (tptp-res-v1:ok (fo-resolution:clauses-cons ?clause ?clauses)))) (body))
    (rule prepend-refused (head (metta-equation
      (tptp-res-v1:prepend ?clause (TPTP:ResolutionInputUnsupportedV1 ?family))
      (TPTP:ResolutionInputUnsupportedV1 ?family))) (body))
    (rule inputs (head (metta-equation
      (tptp-res-v1:inputs (LangDef:ListValue ?items) ?digest ?index)
      (tptp-res-v1:items ?items ?digest ?index))) (body))
    (rule items-nil (head (metta-equation
      (tptp-res-v1:items LNil ?digest ?index)
      (tptp-res-v1:ok (fo-resolution:clauses-nil)))) (body))
    (rule items-cons (head (metta-equation
      (tptp-res-v1:items (LCons ?record ?rest) ?digest ?index)
      (tptp-res-v1:input (langdef:value-view ?record) ?rest ?digest ?index))) (body))
    (rule input (head (metta-equation
      (tptp-res-v1:input (LangDef:ExpressionValue (LCons ?family ?fields)) ?rest ?digest ?index)
      (tptp-res-v1:input-of (langdef:ground-equal ?family cnf) ?family ?fields ?rest
        (fo-resolution:occurrence (fo-resolution:source-digest ?digest) ?index)))) (body))
    (rule input-clause (head (metta-equation
      (tptp-res-v1:input-of LangDef:SameValue ?family
        (LCons ?name (LCons ?role (LCons ?clause ?annotations))) ?rest
        (fo-resolution:occurrence (fo-resolution:source-digest ?digest) ?index))
      (tptp-res-v1:prepend
        (tptp-res-v1:clause
          (fo-resolution:occurrence (fo-resolution:source-digest ?digest) ?index)
          ?name ?role ?clause)
        (tptp-res-v1:items ?rest ?digest (+ ?index 1))))) (body))
    (rule input-other (head (metta-equation
      (tptp-res-v1:input-of LangDef:DifferentValue ?family ?fields ?rest ?occurrence)
      (TPTP:ResolutionInputUnsupportedV1 ?family))) (body))

    ; --------------------------------------------------- expanded document
    (rule expanded-problem (head (metta-equation
      (tptp-res-v1:expanded-problem ?digest (TPTP:ExpandedDocumentV1 ?root ?inputs ?edges))
      (tptp-res-v1:problem-of ?digest (tptp-res-v1:expanded-inputs ?inputs)))) (body))
    (rule expanded-inputs-nil (head (metta-equation
      (tptp-res-v1:expanded-inputs (tptp-ci-v1:expanded-inputs-nil))
      (tptp-res-v1:ok (fo-resolution:clauses-nil)))) (body))
    (rule expanded-inputs-cons (head (metta-equation
      (tptp-res-v1:expanded-inputs
        (tptp-ci-v1:expanded-inputs-cons
          (TPTP:ExpandedInputV1 (TPTP:SourceOccurrenceV1 ?path ?digest ?index ?span)
            ?include-path ?namespace ?qualified ?record)
          ?rest))
      (tptp-res-v1:expanded-input (langdef:value-view ?record) ?rest
        (tptp-res-v1:occurrence-at ?digest ?index ?include-path)))) (body))
    (rule expanded-input (head (metta-equation
      (tptp-res-v1:expanded-input (LangDef:ExpressionValue (LCons ?family ?fields)) ?rest ?occurrence)
      (tptp-res-v1:expanded-input-of (langdef:ground-equal ?family cnf) ?family ?fields ?rest
        ?occurrence))) (body))
    (rule expanded-input-clause (head (metta-equation
      (tptp-res-v1:expanded-input-of LangDef:SameValue ?family
        (LCons ?name (LCons ?role (LCons ?clause ?annotations))) ?rest ?occurrence)
      (tptp-res-v1:prepend
        (tptp-res-v1:clause ?occurrence ?name ?role ?clause)
        (tptp-res-v1:expanded-inputs ?rest)))) (body))
    (rule expanded-input-other (head (metta-equation
      (tptp-res-v1:expanded-input-of LangDef:DifferentValue ?family ?fields ?rest ?occurrence)
      (TPTP:ResolutionInputUnsupportedV1 ?family))) (body))
    (rule occurrence-root (head (metta-equation
      (tptp-res-v1:occurrence-at ?digest ?index (TPTP:IncludePathRootV1 ?root))
      (fo-resolution:occurrence (fo-resolution:source-digest ?digest) ?index))) (body))
    (rule occurrence-included (head (metta-equation
      (tptp-res-v1:occurrence-at ?digest ?index
        (TPTP:IncludePathStepV1 ?parent
          (TPTP:SourceOccurrenceV1 ?parent-path ?parent-digest ?parent-index ?span)
          ?target))
      (fo-resolution:included-occurrence (fo-resolution:source-digest ?digest) ?index
        (tptp-res-v1:occurrence-at ?parent-digest ?parent-index ?parent)))) (body))

    ; --------------------------------------------- formulas: the request
    ; A record's clausification request: its named formula, its polarity and
    ; its binders' distinct names.
    (rule clausification-request (head (metta-equation
      (tptp-res-v1:clausification-request ?record)
      (tptp-res-v1:clausification-request-of (langdef:value-view ?record) ?record))) (body))
    (rule clausification-request-of (head (metta-equation
      (tptp-res-v1:clausification-request-of (LangDef:ExpressionValue (LCons ?family ?fields)) ?record)
      (tptp-res-v1:clausification-fof (langdef:ground-equal ?family fof) ?family ?fields
        ?record))) (body))
    (rule clausification-fof (head (metta-equation
      (tptp-res-v1:clausification-fof LangDef:SameValue ?family
        (LCons ?name (LCons ?role (LCons ?formula ?annotations))) ?record)
      (tptp-res-v1:request-with (tptp-res-v1:clausification-polarity ?role) ?role ?formula
        ?record))) (body))
    (rule clausification-other (head (metta-equation
      (tptp-res-v1:clausification-fof LangDef:DifferentValue ?family ?fields ?record)
      (TPTP:ResolutionInputUnsupportedV1 ?family))) (body))
    (rule request-refused (head (metta-equation
      (tptp-res-v1:request-with refused ?role ?formula ?record)
      (TPTP:ClausificationInputUnsupportedV1 unsupported-role ?role))) (body))
    (rule request-positive (head (metta-equation
      (tptp-res-v1:request-with (tptp-fof-batch:positive) ?role ?formula ?record)
      (TPTP:ClausificationRequestV1 (tptp-fof-batch:positive)
        (tptp-res-v1:named-formula ?formula)
        (tptp-res-v1:binder-names ?formula LNil)))) (body))
    (rule request-negative (head (metta-equation
      (tptp-res-v1:request-with (tptp-fof-batch:negative) ?role ?formula ?record)
      (TPTP:ClausificationRequestV1 (tptp-fof-batch:negative)
        (tptp-res-v1:named-formula ?formula)
        (tptp-res-v1:binder-names ?formula LNil)))) (body))
    ; A conjecture is negated; a type or interpretation record and the unknown
    ; role are not clausified; every other role is asserted.
    (rule clausification-polarity (head (metta-equation
      (tptp-res-v1:clausification-polarity ?role)
      (tptp-res-v1:polarity-at (tptp-res-v1:role ?role) (tptp-res-v1:role-polarities)))) (body))
    (rule polarity-at (head (metta-equation
      (tptp-res-v1:polarity-at (fo-resolution:role ?base) ?table)
      (tptp-res-v1:lookup ?base ?table))) (body))
    (rule role-polarities (head (metta-equation
      (tptp-res-v1:role-polarities)
      (LCons (tptp-res-v1:pair "conjecture" (tptp-fof-batch:negative))
      (LCons (tptp-res-v1:pair "axiom" (tptp-fof-batch:positive))
      (LCons (tptp-res-v1:pair "hypothesis" (tptp-fof-batch:positive))
      (LCons (tptp-res-v1:pair "definition" (tptp-fof-batch:positive))
      (LCons (tptp-res-v1:pair "assumption" (tptp-fof-batch:positive))
      (LCons (tptp-res-v1:pair "lemma" (tptp-fof-batch:positive))
      (LCons (tptp-res-v1:pair "theorem" (tptp-fof-batch:positive))
      (LCons (tptp-res-v1:pair "corollary" (tptp-fof-batch:positive))
      (LCons (tptp-res-v1:pair "negated_conjecture" (tptp-fof-batch:positive))
      (LCons (tptp-res-v1:pair "plain" (tptp-fof-batch:positive))
      (LCons (tptp-res-v1:pair "type" refused)
      (LCons (tptp-res-v1:pair "interpretation" refused)
      (LCons (tptp-res-v1:pair "fi_domain" refused)
      (LCons (tptp-res-v1:pair "fi_functors" refused)
      (LCons (tptp-res-v1:pair "fi_predicates" refused)
      (LCons (tptp-res-v1:pair "unknown" refused)
        LNil)))))))))))))))))) (body))
    (rule polarity-absent (head (metta-equation
      (tptp-res-v1:request-with absent ?role ?formula ?record)
      (TPTP:ClausificationInputUnsupportedV1 unsupported-role ?role))) (body))

    ; --------------------------------------------- formulas: named form
    (rule named-formula (head (metta-equation
      (tptp-res-v1:named-formula ?formula)
      (tptp-res-v1:named-formula-of (langdef:value-view ?formula) ?formula))) (body))
    (rule named-formula-expression (head (metta-equation
      (tptp-res-v1:named-formula-of (LangDef:ExpressionValue (LCons ?head ?arguments)) ?formula)
      (tptp-res-v1:named-at (tptp-res-v1:formula-kind ?head) ?head ?arguments))) (body))
    (rule formula-kind (head (metta-equation
      (tptp-res-v1:formula-kind ?head)
      (tptp-res-v1:formula-kind-of (langdef:value-view ?head) ?head))) (body))
    (rule formula-kind-expression (head (metta-equation
      (tptp-res-v1:formula-kind-of (LangDef:ExpressionValue ?items) ?head) apply)) (body))
    (rule formula-kind-symbol (head (metta-equation
      (tptp-res-v1:formula-kind-of (LangDef:SymbolValue ?spelling) ?head)
      (tptp-res-v1:lookup ?head (tptp-res-v1:formula-kinds)))) (body))
    (rule formula-kinds (head (metta-equation
      (tptp-res-v1:formula-kinds)
      (LCons (tptp-res-v1:pair ∀ universal) (LCons (tptp-res-v1:pair ∃ existential)
      (LCons (tptp-res-v1:pair ∧ conjunction) (LCons (tptp-res-v1:pair ∨ disjunction)
      (LCons (tptp-res-v1:pair → implication) (LCons (tptp-res-v1:pair ← reverse-implication)
      (LCons (tptp-res-v1:pair ↔ equivalence) (LCons (tptp-res-v1:pair ⊕ exclusive-or)
      (LCons (tptp-res-v1:pair ⊽ joint-denial) (LCons (tptp-res-v1:pair ⊼ alternative-denial)
      (LCons (tptp-res-v1:pair ¬ negation) (LCons (tptp-res-v1:pair ≈ equality)
      (LCons (tptp-res-v1:pair ≉ disequality) (LCons (tptp-res-v1:pair word proposition)
      (LCons (tptp-res-v1:pair defined proposition) (LCons (tptp-res-v1:pair system proposition)
        LNil)))))))))))))))))) (body))
    (rule named-universal (head (metta-equation
      (tptp-res-v1:named-at universal ?head (LCons ?binders (LCons ?body LNil)))
      (tptp-res-v1:named-binders universal (langdef:value-view ?binders) ?body))) (body))
    (rule named-existential (head (metta-equation
      (tptp-res-v1:named-at existential ?head (LCons ?binders (LCons ?body LNil)))
      (tptp-res-v1:named-binders existential (langdef:value-view ?binders) ?body))) (body))
    (rule named-binders-universal (head (metta-equation
      (tptp-res-v1:named-binders universal (LangDef:ListValue ?names) ?body)
      (tptp-res-v1:named-all ?names ?body))) (body))
    (rule named-binders-existential (head (metta-equation
      (tptp-res-v1:named-binders existential (LangDef:ListValue ?names) ?body)
      (tptp-res-v1:named-ex ?names ?body))) (body))
    (rule named-all-nil (head (metta-equation
      (tptp-res-v1:named-all LNil ?body) (tptp-res-v1:named-formula ?body))) (body))
    (rule named-all-cons (head (metta-equation
      (tptp-res-v1:named-all (LCons ?binder ?rest) ?body)
      (tptp-fof-named:all (tptp-res-v1:binder-name (langdef:value-view ?binder))
        (tptp-res-v1:named-all ?rest ?body)))) (body))
    (rule named-ex-nil (head (metta-equation
      (tptp-res-v1:named-ex LNil ?body) (tptp-res-v1:named-formula ?body))) (body))
    (rule named-ex-cons (head (metta-equation
      (tptp-res-v1:named-ex (LCons ?binder ?rest) ?body)
      (tptp-fof-named:ex (tptp-res-v1:binder-name (langdef:value-view ?binder))
        (tptp-res-v1:named-ex ?rest ?body)))) (body))
    (rule binder-name (head (metta-equation
      (tptp-res-v1:binder-name (LangDef:ExpressionValue (LCons var (LCons ?text LNil))))
      (tptp-fof-named:name ?text))) (body))
    (rule named-conjunction (head (metta-equation
      (tptp-res-v1:named-at conjunction ?head ?arguments)
      (tptp-res-v1:named-and ?arguments))) (body))
    (rule named-disjunction (head (metta-equation
      (tptp-res-v1:named-at disjunction ?head ?arguments)
      (tptp-res-v1:named-or ?arguments))) (body))
    (rule named-and-last (head (metta-equation
      (tptp-res-v1:named-and (LCons ?only LNil)) (tptp-res-v1:named-formula ?only))) (body))
    (rule named-and-more (head (metta-equation
      (tptp-res-v1:named-and (LCons ?first (LCons ?second ?rest)))
      (tptp-fof-named:and (tptp-res-v1:named-formula ?first)
        (tptp-res-v1:named-and (LCons ?second ?rest))))) (body))
    (rule named-or-last (head (metta-equation
      (tptp-res-v1:named-or (LCons ?only LNil)) (tptp-res-v1:named-formula ?only))) (body))
    (rule named-or-more (head (metta-equation
      (tptp-res-v1:named-or (LCons ?first (LCons ?second ?rest)))
      (tptp-fof-named:or (tptp-res-v1:named-formula ?first)
        (tptp-res-v1:named-or (LCons ?second ?rest))))) (body))
    (rule named-implication (head (metta-equation
      (tptp-res-v1:named-at implication ?head (LCons ?left (LCons ?right LNil)))
      (tptp-fof-named:implies (tptp-res-v1:named-formula ?left)
        (tptp-res-v1:named-formula ?right)))) (body))
    (rule named-reverse-implication (head (metta-equation
      (tptp-res-v1:named-at reverse-implication ?head (LCons ?left (LCons ?right LNil)))
      (tptp-fof-named:reverse-implies (tptp-res-v1:named-formula ?left)
        (tptp-res-v1:named-formula ?right)))) (body))
    (rule named-equivalence (head (metta-equation
      (tptp-res-v1:named-at equivalence ?head (LCons ?left (LCons ?right LNil)))
      (tptp-fof-named:iff (tptp-res-v1:named-formula ?left)
        (tptp-res-v1:named-formula ?right)))) (body))
    (rule named-exclusive-or (head (metta-equation
      (tptp-res-v1:named-at exclusive-or ?head (LCons ?left (LCons ?right LNil)))
      (tptp-fof-named:xor (tptp-res-v1:named-formula ?left)
        (tptp-res-v1:named-formula ?right)))) (body))
    (rule named-joint-denial (head (metta-equation
      (tptp-res-v1:named-at joint-denial ?head (LCons ?left (LCons ?right LNil)))
      (tptp-fof-named:nor (tptp-res-v1:named-formula ?left)
        (tptp-res-v1:named-formula ?right)))) (body))
    (rule named-alternative-denial (head (metta-equation
      (tptp-res-v1:named-at alternative-denial ?head (LCons ?left (LCons ?right LNil)))
      (tptp-fof-named:nand (tptp-res-v1:named-formula ?left)
        (tptp-res-v1:named-formula ?right)))) (body))
    (rule named-negation (head (metta-equation
      (tptp-res-v1:named-at negation ?head (LCons ?body LNil))
      (tptp-fof-named:not (tptp-res-v1:named-formula ?body)))) (body))
    (rule named-equality (head (metta-equation
      (tptp-res-v1:named-at equality ?head (LCons ?left (LCons ?right LNil)))
      (tptp-fof-named:equal (tptp-res-v1:named-term ?left) (tptp-res-v1:named-term ?right)))) (body))
    (rule named-disequality (head (metta-equation
      (tptp-res-v1:named-at disequality ?head (LCons ?left (LCons ?right LNil)))
      (tptp-fof-named:not
        (tptp-fof-named:equal (tptp-res-v1:named-term ?left) (tptp-res-v1:named-term ?right))))) (body))
    ; A proposition is a word, a defined word or a system word: $true and
    ; $false are the truth values.
    (rule named-proposition (head (metta-equation
      (tptp-res-v1:named-at proposition ?kind (LCons ?text LNil))
      (tptp-res-v1:named-truth (tptp-res-v1:lookup (LCons ?kind (LCons ?text LNil))
          (LCons (tptp-res-v1:pair (LCons defined (LCons "true" LNil)) (tptp-fof-named:verum))
          (LCons (tptp-res-v1:pair (LCons defined (LCons "false" LNil)) (tptp-fof-named:falsum))
            LNil)))
        (LCons ?kind (LCons ?text LNil))))) (body))
    (rule named-truth-constant (head (metta-equation
      (tptp-res-v1:named-truth (tptp-fof-named:verum) ?leaf) (tptp-fof-named:verum))) (body))
    (rule named-falsity-constant (head (metta-equation
      (tptp-res-v1:named-truth (tptp-fof-named:falsum) ?leaf) (tptp-fof-named:falsum))) (body))
    (rule named-truth-atom (head (metta-equation
      (tptp-res-v1:named-truth absent (LCons ?kind (LCons ?text LNil)))
      (tptp-fof-named:predicate (tptp-res-v1:predicate-head-of (LCons ?kind (LCons ?text LNil)))
        (tptp-fof-named:terms-nil)))) (body))
    (rule named-apply (head (metta-equation
      (tptp-res-v1:named-at apply ?head ?arguments)
      (tptp-fof-named:predicate (tptp-res-v1:predicate-head ?head)
        (tptp-res-v1:named-terms ?arguments)))) (body))
    (rule predicate-head (head (metta-equation
      (tptp-res-v1:predicate-head ?head)
      (tptp-res-v1:predicate-head-of (tptp-res-v1:items-of (langdef:value-view ?head))))) (body))
    (rule literals-of (head (metta-equation
      (tptp-res-v1:items-of (LangDef:ExpressionValue ?items)) ?items)) (body))
    (rule predicate-head-word (head (metta-equation
      (tptp-res-v1:predicate-head-of (LCons word (LCons ?text LNil)))
      (tptp-fof-symbol:predicate-plain ?text))) (body))
    (rule predicate-head-defined (head (metta-equation
      (tptp-res-v1:predicate-head-of (LCons defined (LCons ?text LNil)))
      (tptp-fof-symbol:predicate-defined ?text))) (body))
    (rule predicate-head-system (head (metta-equation
      (tptp-res-v1:predicate-head-of (LCons system (LCons ?text LNil)))
      (tptp-fof-symbol:predicate-system ?text))) (body))

    ; ---------------------------------------------- formulas: named terms
    (rule named-terms-nil (head (metta-equation
      (tptp-res-v1:named-terms LNil) (tptp-fof-named:terms-nil))) (body))
    (rule named-terms-cons (head (metta-equation
      (tptp-res-v1:named-terms (LCons ?term ?rest))
      (tptp-fof-named:terms-cons (tptp-res-v1:named-term ?term)
        (tptp-res-v1:named-terms ?rest)))) (body))
    (rule named-term (head (metta-equation
      (tptp-res-v1:named-term ?term)
      (tptp-res-v1:named-term-of (langdef:value-view ?term) ?term))) (body))
    (rule named-term-integer (head (metta-equation
      (tptp-res-v1:named-term-of (LangDef:IntegerValue ?text) ?term)
      (tptp-fof-named:term-function (tptp-fof-symbol:function-integer ?text)
        (tptp-fof-named:terms-nil)))) (body))
    (rule named-term-expression (head (metta-equation
      (tptp-res-v1:named-term-of (LangDef:ExpressionValue (LCons ?head ?arguments)) ?term)
      (tptp-res-v1:named-term-at (tptp-res-v1:head-kind ?head) ?head ?arguments))) (body))
    (rule named-term-variable (head (metta-equation
      (tptp-res-v1:named-term-at variable ?head (LCons ?text LNil))
      (tptp-fof-named:term-variable (tptp-fof-named:name ?text)))) (body))
    (rule named-term-leaf (head (metta-equation
      (tptp-res-v1:named-term-at leaf ?kind (LCons ?text LNil))
      (tptp-fof-named:term-function (tptp-res-v1:function-head-of (LCons ?kind (LCons ?text LNil)))
        (tptp-fof-named:terms-nil)))) (body))
    (rule named-term-apply (head (metta-equation
      (tptp-res-v1:named-term-at apply ?head ?arguments)
      (tptp-fof-named:term-function (tptp-res-v1:function-head ?head)
        (tptp-res-v1:named-terms ?arguments)))) (body))
    (rule named-term-distinct (head (metta-equation
      (tptp-res-v1:named-term-at distinct-object ?head (LCons ?text LNil))
      (tptp-fof-named:term-function (tptp-fof-symbol:function-distinct-object ?text)
        (tptp-fof-named:terms-nil)))) (body))
    (rule named-term-rational (head (metta-equation
      (tptp-res-v1:named-term-at rational ?head (LCons ?numerator (LCons ?denominator LNil)))
      (tptp-fof-named:term-function
        (tptp-fof-symbol:function-rational (tptp-res-v1:rational-lexeme ?numerator ?denominator))
        (tptp-fof-named:terms-nil)))) (body))
    (rule named-term-real (head (metta-equation
      (tptp-res-v1:named-term-at real-number ?head (LCons ?text LNil))
      (tptp-fof-named:term-function (tptp-fof-symbol:function-real ?text)
        (tptp-fof-named:terms-nil)))) (body))
    (rule function-head (head (metta-equation
      (tptp-res-v1:function-head ?head)
      (tptp-res-v1:function-head-of (tptp-res-v1:items-of (langdef:value-view ?head))))) (body))
    (rule function-head-word (head (metta-equation
      (tptp-res-v1:function-head-of (LCons word (LCons ?text LNil)))
      (tptp-fof-symbol:function-plain ?text))) (body))
    (rule function-head-defined (head (metta-equation
      (tptp-res-v1:function-head-of (LCons defined (LCons ?text LNil)))
      (tptp-fof-symbol:function-defined ?text))) (body))
    (rule function-head-system (head (metta-equation
      (tptp-res-v1:function-head-of (LCons system (LCons ?text LNil)))
      (tptp-fof-symbol:function-system ?text))) (body))

    ; ------------------------------------------- formulas: binder names
    ; The distinct names bound anywhere in a formula, in first-binding order.
    (rule binder-names (head (metta-equation
      (tptp-res-v1:binder-names ?formula ?seen)
      (tptp-res-v1:binder-names-of (langdef:value-view ?formula) ?seen))) (body))
    (rule binder-names-expression (head (metta-equation
      (tptp-res-v1:binder-names-of (LangDef:ExpressionValue (LCons ?head ?arguments)) ?seen)
      (tptp-res-v1:binder-names-at (tptp-res-v1:formula-kind ?head) ?head ?arguments ?seen))) (body))
    (rule binder-names-universal (head (metta-equation
      (tptp-res-v1:binder-names-at universal ?head (LCons ?binders (LCons ?body LNil)) ?seen)
      (tptp-res-v1:binder-names ?body
        (tptp-res-v1:binder-names-list (langdef:value-view ?binders) ?seen)))) (body))
    (rule binder-names-existential (head (metta-equation
      (tptp-res-v1:binder-names-at existential ?head (LCons ?binders (LCons ?body LNil)) ?seen)
      (tptp-res-v1:binder-names ?body
        (tptp-res-v1:binder-names-list (langdef:value-view ?binders) ?seen)))) (body))
    (rule binder-names-conjunction (head (metta-equation
      (tptp-res-v1:binder-names-at conjunction ?head ?arguments ?seen)
      (tptp-res-v1:binder-names-all ?arguments ?seen))) (body))
    (rule binder-names-disjunction (head (metta-equation
      (tptp-res-v1:binder-names-at disjunction ?head ?arguments ?seen)
      (tptp-res-v1:binder-names-all ?arguments ?seen))) (body))
    (rule binder-names-implication (head (metta-equation
      (tptp-res-v1:binder-names-at implication ?head ?arguments ?seen)
      (tptp-res-v1:binder-names-all ?arguments ?seen))) (body))
    (rule binder-names-reverse-implication (head (metta-equation
      (tptp-res-v1:binder-names-at reverse-implication ?head ?arguments ?seen)
      (tptp-res-v1:binder-names-all ?arguments ?seen))) (body))
    (rule binder-names-equivalence (head (metta-equation
      (tptp-res-v1:binder-names-at equivalence ?head ?arguments ?seen)
      (tptp-res-v1:binder-names-all ?arguments ?seen))) (body))
    (rule binder-names-exclusive-or (head (metta-equation
      (tptp-res-v1:binder-names-at exclusive-or ?head ?arguments ?seen)
      (tptp-res-v1:binder-names-all ?arguments ?seen))) (body))
    (rule binder-names-joint-denial (head (metta-equation
      (tptp-res-v1:binder-names-at joint-denial ?head ?arguments ?seen)
      (tptp-res-v1:binder-names-all ?arguments ?seen))) (body))
    (rule binder-names-alternative-denial (head (metta-equation
      (tptp-res-v1:binder-names-at alternative-denial ?head ?arguments ?seen)
      (tptp-res-v1:binder-names-all ?arguments ?seen))) (body))
    (rule binder-names-negation (head (metta-equation
      (tptp-res-v1:binder-names-at negation ?head ?arguments ?seen)
      (tptp-res-v1:binder-names-all ?arguments ?seen))) (body))
    (rule binder-names-equality (head (metta-equation
      (tptp-res-v1:binder-names-at equality ?head ?arguments ?seen) ?seen)) (body))
    (rule binder-names-disequality (head (metta-equation
      (tptp-res-v1:binder-names-at disequality ?head ?arguments ?seen) ?seen)) (body))
    (rule binder-names-proposition (head (metta-equation
      (tptp-res-v1:binder-names-at proposition ?head ?arguments ?seen) ?seen)) (body))
    (rule binder-names-apply (head (metta-equation
      (tptp-res-v1:binder-names-at apply ?head ?arguments ?seen) ?seen)) (body))
    (rule binder-names-all-nil (head (metta-equation
      (tptp-res-v1:binder-names-all LNil ?seen) ?seen)) (body))
    (rule binder-names-all-cons (head (metta-equation
      (tptp-res-v1:binder-names-all (LCons ?formula ?rest) ?seen)
      (tptp-res-v1:binder-names-all ?rest (tptp-res-v1:binder-names ?formula ?seen)))) (body))
    (rule binder-names-list (head (metta-equation
      (tptp-res-v1:binder-names-list (LangDef:ListValue ?binders) ?seen)
      (tptp-res-v1:binder-names-all-binders ?binders ?seen))) (body))
    (rule binder-names-binders-nil (head (metta-equation
      (tptp-res-v1:binder-names-all-binders LNil ?seen) ?seen)) (body))
    (rule binder-names-binders-cons (head (metta-equation
      (tptp-res-v1:binder-names-all-binders (LCons ?binder ?rest) ?seen)
      (tptp-res-v1:binder-names-all-binders ?rest
        (tptp-res-v1:add-name (tptp-res-v1:binder-name (langdef:value-view ?binder)) ?seen)))) (body))
    (rule add-name (head (metta-equation
      (tptp-res-v1:add-name (tptp-fof-named:name ?text) ?seen)
      (tptp-res-v1:add-name-at (tptp-res-v1:member-name ?text ?seen) ?text ?seen))) (body))
    (rule add-name-present (head (metta-equation
      (tptp-res-v1:add-name-at True ?text ?seen) ?seen)) (body))
    (rule add-name-absent (head (metta-equation
      (tptp-res-v1:add-name-at False ?text ?seen)
      (tptp-res-v1:join-names ?seen ?text))) (body))
    (rule join-names-nil (head (metta-equation
      (tptp-res-v1:join-names LNil ?text) (LCons ?text LNil))) (body))
    (rule join-names-cons (head (metta-equation
      (tptp-res-v1:join-names (LCons ?first ?rest) ?text)
      (LCons ?first (tptp-res-v1:join-names ?rest ?text)))) (body))
    (rule member-name-nil (head (metta-equation
      (tptp-res-v1:member-name ?text LNil) False)) (body))
    (rule member-name-cons (head (metta-equation
      (tptp-res-v1:member-name ?text (LCons ?first ?rest))
      (tptp-res-v1:member-name-at (langdef:ground-equal ?text ?first) ?text ?rest))) (body))
    (rule member-name-same (head (metta-equation
      (tptp-res-v1:member-name-at LangDef:SameValue ?text ?rest) True)) (body))
    (rule member-name-different (head (metta-equation
      (tptp-res-v1:member-name-at LangDef:DifferentValue ?text ?rest)
      (tptp-res-v1:member-name ?text ?rest))) (body))

    ; ------------------------------------------- formulas: clausal form
    ; The clauses of a formula's clausal form at clausal occurrences.
    (rule clausal-form (head (metta-equation
      (tptp-res-v1:clausal-form ?occurrence ?name ?role ?polarity ?clauses)
      (tptp-res-v1:clausal-clauses ?clauses ?occurrence (tptp-res-v1:name ?name)
        (tptp-res-v1:clausal-role ?polarity ?role) 0))) (body))
    (rule clausal-role-negative (head (metta-equation
      (tptp-res-v1:clausal-role (tptp-fof-batch:negative) ?role)
      (fo-resolution:role "negated_conjecture"))) (body))
    (rule clausal-role-positive (head (metta-equation
      (tptp-res-v1:clausal-role (tptp-fof-batch:positive) ?role)
      (tptp-res-v1:role ?role))) (body))
    (rule clausal-clauses-nil (head (metta-equation
      (tptp-res-v1:clausal-clauses (tptp-fof-cnf:clauses-nil) ?occurrence ?name ?role ?index)
      (tptp-res-v1:ok (fo-resolution:clauses-nil)))) (body))
    (rule clausal-clauses-cons (head (metta-equation
      (tptp-res-v1:clausal-clauses (tptp-fof-cnf:clauses-cons ?clause ?rest)
        ?occurrence ?name ?role ?index)
      (tptp-res-v1:clausal-clause
        (tptp-res-v1:clause-literals ?clause
          (fo-resolution:clausal-occurrence ?occurrence ?index))
        (fo-resolution:clausal-occurrence ?occurrence ?index) ?name ?role
        ?rest ?occurrence))) (body))
    (rule clausal-clause-tautology (head (metta-equation
      (tptp-res-v1:clausal-clause (tptp-res-v1:tautology)
        (fo-resolution:clausal-occurrence ?formula ?index) ?name ?role ?rest ?occurrence)
      (tptp-res-v1:clausal-clauses ?rest ?occurrence ?name ?role (+ ?index 1)))) (body))
    (rule clausal-clause-literals (head (metta-equation
      (tptp-res-v1:clausal-clause (tptp-res-v1:ok ?literals)
        (fo-resolution:clausal-occurrence ?formula ?index) ?name ?role ?rest ?occurrence)
      (tptp-res-v1:prepend
        (fo-resolution:clause (fo-resolution:clausal-occurrence ?formula ?index)
          ?name ?role ?literals)
        (tptp-res-v1:clausal-clauses ?rest ?occurrence ?name ?role (+ ?index 1))))) (body))
    (rule clause-literals-nil (head (metta-equation
      (tptp-res-v1:clause-literals (tptp-fof-cnf:clause-nil) ?occurrence)
      (tptp-res-v1:ok (fo-resolution:literals-nil)))) (body))
    (rule clause-literals-cons (head (metta-equation
      (tptp-res-v1:clause-literals (tptp-fof-cnf:clause-cons ?reference ?rest) ?occurrence)
      (tptp-res-v1:literal-prepend (tptp-res-v1:reference ?reference ?occurrence)
        (tptp-res-v1:clause-literals ?rest ?occurrence)))) (body))
    (rule literal-prepend-literal (head (metta-equation
      (tptp-res-v1:literal-prepend (tptp-res-v1:a-literal ?literal) (tptp-res-v1:ok ?literals))
      (tptp-res-v1:ok (fo-resolution:literals-cons ?literal ?literals)))) (body))
    (rule literal-prepend-after-tautology (head (metta-equation
      (tptp-res-v1:literal-prepend (tptp-res-v1:a-literal ?literal) (tptp-res-v1:tautology))
      (tptp-res-v1:tautology))) (body))
    (rule literal-prepend-true (head (metta-equation
      (tptp-res-v1:literal-prepend (tptp-res-v1:true-reference) ?rest)
      (tptp-res-v1:tautology))) (body))
    (rule literal-prepend-false (head (metta-equation
      (tptp-res-v1:literal-prepend (tptp-res-v1:false-reference) ?rest) ?rest)) (body))
    (rule reference-verum (head (metta-equation
      (tptp-res-v1:reference (tptp-fof-named:ref-verum) ?occurrence)
      (tptp-res-v1:true-reference))) (body))
    (rule reference-falsum (head (metta-equation
      (tptp-res-v1:reference (tptp-fof-named:ref-falsum) ?occurrence)
      (tptp-res-v1:false-reference))) (body))
    (rule reference-original-positive (head (metta-equation
      (tptp-res-v1:reference (tptp-fof-named:ref-original-positive ?relation ?arguments) ?occurrence)
      (tptp-res-v1:a-literal (fo-resolution:literal-positive
        (fo-resolution:atom-predicate (tptp-res-v1:predicate-symbol ?relation)
          (tptp-res-v1:skolem-terms ?arguments ?occurrence)))))) (body))
    (rule reference-original-negative (head (metta-equation
      (tptp-res-v1:reference (tptp-fof-named:ref-original-negative ?relation ?arguments) ?occurrence)
      (tptp-res-v1:a-literal (fo-resolution:literal-negative
        (fo-resolution:atom-predicate (tptp-res-v1:predicate-symbol ?relation)
          (tptp-res-v1:skolem-terms ?arguments ?occurrence)))))) (body))
    (rule reference-equal (head (metta-equation
      (tptp-res-v1:reference (tptp-fof-named:ref-equal ?left ?right) ?occurrence)
      (tptp-res-v1:a-literal (fo-resolution:literal-positive
        (fo-resolution:atom-equality (tptp-res-v1:skolem-term ?left ?occurrence)
          (tptp-res-v1:skolem-term ?right ?occurrence)))))) (body))
    (rule reference-not-equal (head (metta-equation
      (tptp-res-v1:reference (tptp-fof-named:ref-not-equal ?left ?right) ?occurrence)
      (tptp-res-v1:a-literal (fo-resolution:literal-negative
        (fo-resolution:atom-equality (tptp-res-v1:skolem-term ?left ?occurrence)
          (tptp-res-v1:skolem-term ?right ?occurrence)))))) (body))
    (rule reference-defined-positive (head (metta-equation
      (tptp-res-v1:reference (tptp-fof-named:ref-defined-positive ?id ?arguments) ?occurrence)
      (tptp-res-v1:a-literal (fo-resolution:literal-positive
        (fo-resolution:atom-predicate
          (fo-resolution:symbol-name (fo-resolution:symbol-definition) (tptp-res-v1:index-text ?id))
          (tptp-res-v1:skolem-terms ?arguments ?occurrence)))))) (body))
    (rule reference-defined-negative (head (metta-equation
      (tptp-res-v1:reference (tptp-fof-named:ref-defined-negative ?id ?arguments) ?occurrence)
      (tptp-res-v1:a-literal (fo-resolution:literal-negative
        (fo-resolution:atom-predicate
          (fo-resolution:symbol-name (fo-resolution:symbol-definition) (tptp-res-v1:index-text ?id))
          (tptp-res-v1:skolem-terms ?arguments ?occurrence)))))) (body))
    (rule predicate-symbol-plain (head (metta-equation
      (tptp-res-v1:predicate-symbol (tptp-fof-symbol:predicate-plain ?text))
      (tptp-res-v1:symbol word ?text))) (body))
    (rule predicate-symbol-defined (head (metta-equation
      (tptp-res-v1:predicate-symbol (tptp-fof-symbol:predicate-defined ?text))
      (tptp-res-v1:symbol defined ?text))) (body))
    (rule predicate-symbol-system (head (metta-equation
      (tptp-res-v1:predicate-symbol (tptp-fof-symbol:predicate-system ?text))
      (tptp-res-v1:symbol system ?text))) (body))
    (rule skolem-terms-nil (head (metta-equation
      (tptp-res-v1:skolem-terms (tptp-fof-skolem:terms-nil) ?occurrence)
      (fo-resolution:terms-nil))) (body))
    (rule skolem-terms-cons (head (metta-equation
      (tptp-res-v1:skolem-terms (tptp-fof-skolem:terms-cons ?term ?rest) ?occurrence)
      (fo-resolution:terms-cons (tptp-res-v1:skolem-term ?term ?occurrence)
        (tptp-res-v1:skolem-terms ?rest ?occurrence)))) (body))
    (rule skolem-term-variable (head (metta-equation
      (tptp-res-v1:skolem-term (tptp-fof-skolem:term-variable ?index) ?occurrence)
      (fo-resolution:term-variable
        (fo-resolution:variable-id ?occurrence
          (fo-resolution:variable-name (tptp-res-v1:variable-text ?index)))))) (body))
    (rule skolem-term-generated (head (metta-equation
      (tptp-res-v1:skolem-term (tptp-fof-skolem:term-generated ?id ?arguments) ?occurrence)
      (fo-resolution:term-function
        (fo-resolution:symbol-name (fo-resolution:symbol-skolem) (tptp-res-v1:index-text ?id))
        (tptp-res-v1:skolem-terms ?arguments ?occurrence)))) (body))
    (rule skolem-term-original (head (metta-equation
      (tptp-res-v1:skolem-term (tptp-fof-skolem:term-original ?function ?arguments) ?occurrence)
      (tptp-res-v1:original-term ?function ?arguments ?occurrence))) (body))
    (rule original-term-plain (head (metta-equation
      (tptp-res-v1:original-term (tptp-fof-symbol:function-plain ?text) ?arguments ?occurrence)
      (fo-resolution:term-function (tptp-res-v1:symbol word ?text)
        (tptp-res-v1:skolem-terms ?arguments ?occurrence)))) (body))
    (rule original-term-defined (head (metta-equation
      (tptp-res-v1:original-term (tptp-fof-symbol:function-defined ?text) ?arguments ?occurrence)
      (fo-resolution:term-function (tptp-res-v1:symbol defined ?text)
        (tptp-res-v1:skolem-terms ?arguments ?occurrence)))) (body))
    (rule original-term-system (head (metta-equation
      (tptp-res-v1:original-term (tptp-fof-symbol:function-system ?text) ?arguments ?occurrence)
      (fo-resolution:term-function (tptp-res-v1:symbol system ?text)
        (tptp-res-v1:skolem-terms ?arguments ?occurrence)))) (body))
    (rule original-term-integer (head (metta-equation
      (tptp-res-v1:original-term (tptp-fof-symbol:function-integer ?text)
        (tptp-fof-skolem:terms-nil) ?occurrence)
      (fo-resolution:term-number (fo-resolution:number-integer) ?text))) (body))
    (rule original-term-rational (head (metta-equation
      (tptp-res-v1:original-term (tptp-fof-symbol:function-rational ?text)
        (tptp-fof-skolem:terms-nil) ?occurrence)
      (fo-resolution:term-number (fo-resolution:number-rational) ?text))) (body))
    (rule original-term-real (head (metta-equation
      (tptp-res-v1:original-term (tptp-fof-symbol:function-real ?text)
        (tptp-fof-skolem:terms-nil) ?occurrence)
      (fo-resolution:term-number (fo-resolution:number-real) ?text))) (body))
    (rule original-term-distinct (head (metta-equation
      (tptp-res-v1:original-term (tptp-fof-symbol:function-distinct-object ?text)
        (tptp-fof-skolem:terms-nil) ?occurrence)
      (fo-resolution:term-distinct ?text))) (body))
    (rule index-integer-zero (head (metta-equation
      (tptp-res-v1:index-integer (tptp-fof-resolved:index-zero)) 0)) (body))
    (rule index-integer-succ (head (metta-equation
      (tptp-res-v1:index-integer (tptp-fof-resolved:index-succ ?index))
      (+ 1 (tptp-res-v1:index-integer ?index)))) (body))
    (rule index-text (head (metta-equation
      (tptp-res-v1:index-text ?index)
      (tptp-res-v1:integer-text (tptp-res-v1:index-integer ?index)))) (body))
    ; An opened universal is named X and its index: X0 is the innermost.
    (rule variable-text (head (metta-equation
      (tptp-res-v1:variable-text ?index)
      (str:concat "X" (tptp-res-v1:index-text ?index)))) (body))

    ; ------------------------------------------------ inputs with occurrences
    ; The inputs of a document or of an expanded document, each with the
    ; occurrence it has in the resolution input.
    (rule document-items (head (metta-equation
      (tptp-res-v1:document-items ?records ?digest ?index)
      (tptp-res-v1:document-items-of (langdef:value-view ?records) ?digest ?index))) (body))
    (rule document-items-nil (head (metta-equation
      (tptp-res-v1:document-items-of (LangDef:ListValue LNil) ?digest ?index) LNil)) (body))
    (rule document-items-cons (head (metta-equation
      (tptp-res-v1:document-items-of (LangDef:ListValue (LCons ?record ?rest)) ?digest ?index)
      (LCons (tptp-res-v1:item
               (fo-resolution:occurrence (fo-resolution:source-digest ?digest) ?index) ?record)
        (tptp-res-v1:document-items-of (LangDef:ListValue ?rest) ?digest (+ ?index 1))))) (body))
    (rule expanded-items-nil (head (metta-equation
      (tptp-res-v1:expanded-items (tptp-ci-v1:expanded-inputs-nil)) LNil)) (body))
    (rule expanded-items-cons (head (metta-equation
      (tptp-res-v1:expanded-items
        (tptp-ci-v1:expanded-inputs-cons
          (TPTP:ExpandedInputV1 (TPTP:SourceOccurrenceV1 ?path ?digest ?index ?span)
            ?include-path ?namespace ?qualified ?record)
          ?rest))
      (LCons (tptp-res-v1:item (tptp-res-v1:occurrence-at ?digest ?index ?include-path) ?record)
        (tptp-res-v1:expanded-items ?rest)))) (body))

    ; ------------------------------------------------------ one record
    (rule record-clause (head (metta-equation
      (tptp-res-v1:record-clause ?occurrence ?record)
      (tptp-res-v1:record-clause-of (langdef:value-view ?record) ?occurrence))) (body))
    (rule record-clause-of (head (metta-equation
      (tptp-res-v1:record-clause-of (LangDef:ExpressionValue (LCons ?family ?fields)) ?occurrence)
      (tptp-res-v1:record-clause-at (langdef:ground-equal ?family cnf) ?family ?fields
        ?occurrence))) (body))
    (rule record-clause-cnf (head (metta-equation
      (tptp-res-v1:record-clause-at LangDef:SameValue ?family
        (LCons ?name (LCons ?role (LCons ?clause ?annotations))) ?occurrence)
      (tptp-res-v1:clause ?occurrence ?name ?role ?clause))) (body))
    (rule record-clause-other (head (metta-equation
      (tptp-res-v1:record-clause-at LangDef:DifferentValue ?family ?fields ?occurrence)
      (TPTP:ResolutionInputUnsupportedV1 ?family))) (body))

    ; ------------------------------------------------------------- clauses
    (rule clause (head (metta-equation
      (tptp-res-v1:clause ?occurrence ?name ?role ?clause)
      (fo-resolution:clause ?occurrence (tptp-res-v1:name ?name)
        (tptp-res-v1:role ?role)
        (tptp-res-v1:disjunction (langdef:value-view ?clause) ?occurrence)))) (body))
    (rule disjunction (head (metta-equation
      (tptp-res-v1:disjunction (LangDef:ExpressionValue (LCons ∨ ?literals)) ?occurrence)
      (tptp-res-v1:literals ?literals ?occurrence))) (body))
    (rule literals-nil (head (metta-equation
      (tptp-res-v1:literals LNil ?occurrence)
      (fo-resolution:literals-nil))) (body))
    (rule literals-cons (head (metta-equation
      (tptp-res-v1:literals (LCons ?literal ?rest) ?occurrence)
      (fo-resolution:literals-cons
        (tptp-res-v1:literal (langdef:value-view ?literal) ?occurrence)
        (tptp-res-v1:literals ?rest ?occurrence)))) (body))
    (rule literal (head (metta-equation
      (tptp-res-v1:literal (LangDef:ExpressionValue (LCons ?head ?arguments)) ?occurrence)
      (tptp-res-v1:literal-at (tptp-res-v1:head-kind ?head) ?head ?arguments ?occurrence))) (body))
    (rule literal-not (head (metta-equation
      (tptp-res-v1:literal-at not ?head (LCons ?atom LNil) ?occurrence)
      (fo-resolution:literal-negative
        (tptp-res-v1:atom (langdef:value-view ?atom) ?occurrence)))) (body))
    (rule literal-unequal (head (metta-equation
      (tptp-res-v1:literal-at unequal ?head (LCons ?left (LCons ?right LNil)) ?occurrence)
      (fo-resolution:literal-negative
        (fo-resolution:atom-equality
          (tptp-res-v1:term ?left ?occurrence) (tptp-res-v1:term ?right ?occurrence))))) (body))
    (rule literal-equal (head (metta-equation
      (tptp-res-v1:literal-at equal ?head ?arguments ?occurrence)
      (fo-resolution:literal-positive
        (tptp-res-v1:atom-at equal ?head ?arguments ?occurrence)))) (body))
    (rule literal-leaf (head (metta-equation
      (tptp-res-v1:literal-at leaf ?head ?arguments ?occurrence)
      (fo-resolution:literal-positive
        (tptp-res-v1:atom-at leaf ?head ?arguments ?occurrence)))) (body))
    (rule literal-apply (head (metta-equation
      (tptp-res-v1:literal-at apply ?head ?arguments ?occurrence)
      (fo-resolution:literal-positive
        (tptp-res-v1:atom-at apply ?head ?arguments ?occurrence)))) (body))

    ; --------------------------------------------------------------- atoms
    (rule atom (head (metta-equation
      (tptp-res-v1:atom (LangDef:ExpressionValue (LCons ?head ?arguments)) ?occurrence)
      (tptp-res-v1:atom-at (tptp-res-v1:head-kind ?head) ?head ?arguments ?occurrence))) (body))
    (rule atom-equal (head (metta-equation
      (tptp-res-v1:atom-at equal ?head (LCons ?left (LCons ?right LNil)) ?occurrence)
      (fo-resolution:atom-equality
        (tptp-res-v1:term ?left ?occurrence) (tptp-res-v1:term ?right ?occurrence)))) (body))
    (rule atom-leaf (head (metta-equation
      (tptp-res-v1:atom-at leaf ?head (LCons ?text LNil) ?occurrence)
      (fo-resolution:atom-predicate (tptp-res-v1:symbol ?head ?text)
        (fo-resolution:terms-nil)))) (body))
    (rule atom-apply (head (metta-equation
      (tptp-res-v1:atom-at apply ?head ?arguments ?occurrence)
      (fo-resolution:atom-predicate (tptp-res-v1:leaf-symbol (langdef:value-view ?head))
        (tptp-res-v1:terms ?arguments ?occurrence)))) (body))

    ; --------------------------------------------------------------- terms
    (rule terms-nil (head (metta-equation
      (tptp-res-v1:terms LNil ?occurrence)
      (fo-resolution:terms-nil))) (body))
    (rule terms-cons (head (metta-equation
      (tptp-res-v1:terms (LCons ?term ?rest) ?occurrence)
      (fo-resolution:terms-cons (tptp-res-v1:term ?term ?occurrence)
        (tptp-res-v1:terms ?rest ?occurrence)))) (body))
    (rule term (head (metta-equation
      (tptp-res-v1:term ?term ?occurrence)
      (tptp-res-v1:term-of (langdef:value-view ?term) ?term ?occurrence))) (body))
    (rule term-integer (head (metta-equation
      (tptp-res-v1:term-of (LangDef:IntegerValue ?text) ?term ?occurrence)
      (fo-resolution:term-number (fo-resolution:number-integer) ?text))) (body))
    (rule term-expression (head (metta-equation
      (tptp-res-v1:term-of (LangDef:ExpressionValue (LCons ?head ?arguments)) ?term ?occurrence)
      (tptp-res-v1:term-at (tptp-res-v1:head-kind ?head) ?head ?arguments ?occurrence))) (body))
    (rule term-variable (head (metta-equation
      (tptp-res-v1:term-at variable ?head (LCons ?name LNil) ?occurrence)
      (fo-resolution:term-variable
        (fo-resolution:variable-id ?occurrence (fo-resolution:variable-name ?name))))) (body))
    (rule term-leaf (head (metta-equation
      (tptp-res-v1:term-at leaf ?head (LCons ?text LNil) ?occurrence)
      (fo-resolution:term-function (tptp-res-v1:symbol ?head ?text)
        (fo-resolution:terms-nil)))) (body))
    (rule term-apply (head (metta-equation
      (tptp-res-v1:term-at apply ?head ?arguments ?occurrence)
      (fo-resolution:term-function (tptp-res-v1:leaf-symbol (langdef:value-view ?head))
        (tptp-res-v1:terms ?arguments ?occurrence)))) (body))
    (rule term-distinct (head (metta-equation
      (tptp-res-v1:term-at distinct-object ?head (LCons ?text LNil) ?occurrence)
      (fo-resolution:term-distinct ?text))) (body))
    (rule term-rational (head (metta-equation
      (tptp-res-v1:term-at rational ?head (LCons ?numerator (LCons ?denominator LNil)) ?occurrence)
      (fo-resolution:term-number (fo-resolution:number-rational)
        (tptp-res-v1:rational-lexeme ?numerator ?denominator)))) (body))
    (rule term-real (head (metta-equation
      (tptp-res-v1:term-at real-number ?head (LCons ?text LNil) ?occurrence)
      (fo-resolution:term-number (fo-resolution:number-real) ?text))) (body))

    ; ------------------------------------------------------------- symbols
    ; A leaf at a head position: (word "p"), (defined "p") or (system "p").
    (rule leaf-symbol (head (metta-equation
      (tptp-res-v1:leaf-symbol (LangDef:ExpressionValue (LCons ?tag (LCons ?text LNil))))
      (tptp-res-v1:symbol ?tag ?text))) (body))
    (rule symbol-word (head (metta-equation
      (tptp-res-v1:symbol word ?text)
      (fo-resolution:symbol-name (tptp-res-v1:word-kind ?text) ?text))) (body))
    (rule symbol-defined (head (metta-equation
      (tptp-res-v1:symbol defined ?text)
      (fo-resolution:symbol-name (fo-resolution:symbol-defined) ?text))) (body))
    (rule symbol-system (head (metta-equation
      (tptp-res-v1:symbol system ?text)
      (fo-resolution:symbol-name (fo-resolution:symbol-system) ?text))) (body))
    (rule word-kind (head (metta-equation
      (tptp-res-v1:word-kind ?text)
      (tptp-res-v1:word-kind-of (str:lower-word? ?text)))) (body))
    (rule word-kind-lower (head (metta-equation
      (tptp-res-v1:word-kind-of True) (fo-resolution:symbol-lower))) (body))
    (rule word-kind-quoted (head (metta-equation
      (tptp-res-v1:word-kind-of False) (fo-resolution:symbol-quoted))) (body))

    ; ----------------------------------------------------------- head kinds
    ; An expression at a head position is applied; a symbol is looked up.
    (rule head-kind (head (metta-equation
      (tptp-res-v1:head-kind ?head)
      (tptp-res-v1:head-kind-of (langdef:value-view ?head) ?head))) (body))
    (rule head-kind-expression (head (metta-equation
      (tptp-res-v1:head-kind-of (LangDef:ExpressionValue ?items) ?head) apply)) (body))
    (rule head-kind-symbol (head (metta-equation
      (tptp-res-v1:head-kind-of (LangDef:SymbolValue ?spelling) ?head)
      (tptp-res-v1:lookup ?head (tptp-res-v1:head-kinds)))) (body))
    (rule head-kinds (head (metta-equation
      (tptp-res-v1:head-kinds)
      (LCons (tptp-res-v1:pair ¬ not) (LCons (tptp-res-v1:pair ≈ equal)
      (LCons (tptp-res-v1:pair ≉ unequal) (LCons (tptp-res-v1:pair var variable)
      (LCons (tptp-res-v1:pair word leaf) (LCons (tptp-res-v1:pair defined leaf)
      (LCons (tptp-res-v1:pair system leaf) (LCons (tptp-res-v1:pair distinct distinct-object)
      (LCons (tptp-res-v1:pair rat rational) (LCons (tptp-res-v1:pair real real-number)
        LNil)))))))))))) (body))
    (rule lookup-absent (head (metta-equation (tptp-res-v1:lookup ?key LNil) absent)) (body))
    (rule lookup-next (head (metta-equation
      (tptp-res-v1:lookup ?key (LCons (tptp-res-v1:pair ?candidate ?value) ?rest))
      (tptp-res-v1:lookup-at (langdef:ground-equal ?key ?candidate) ?value ?key ?rest))) (body))
    (rule lookup-found (head (metta-equation
      (tptp-res-v1:lookup-at LangDef:SameValue ?value ?key ?rest) ?value)) (body))
    (rule lookup-rest (head (metta-equation
      (tptp-res-v1:lookup-at LangDef:DifferentValue ?value ?key ?rest)
      (tptp-res-v1:lookup ?key ?rest))) (body))

    ; ------------------------------------------------------ names and roles
    (rule name (head (metta-equation
      (tptp-res-v1:name ?name) (tptp-res-v1:name-of (langdef:value-view ?name)))) (body))
    (rule name-word (head (metta-equation
      (tptp-res-v1:name-of (LangDef:ExpressionValue (LCons word (LCons ?text LNil))))
      (fo-resolution:formula-name-atomic
        (fo-resolution:symbol-name (tptp-res-v1:word-kind ?text) ?text)))) (body))
    (rule name-integer (head (metta-equation
      (tptp-res-v1:name-of (LangDef:IntegerValue ?text))
      (fo-resolution:formula-name-integer ?text))) (body))
    ; A refined role lower-general has the base role lower.
    (rule role (head (metta-equation
      (tptp-res-v1:role ?role) (tptp-res-v1:role-of (langdef:value-view ?role)))) (body))
    (rule role-symbol (head (metta-equation
      (tptp-res-v1:role-of (LangDef:SymbolValue ?spelling))
      (fo-resolution:role ?spelling))) (body))
    (rule role-refined (head (metta-equation
      (tptp-res-v1:role-of (LangDef:ExpressionValue (LCons ?base ?general)))
      (tptp-res-v1:role ?base))) (body))

    ; ------------------------------------------------------------ numerals
    (rule integer-text (head (metta-equation
      (tptp-res-v1:integer-text ?n) (tptp-res-v1:integer-text-of (langdef:value-view ?n)))) (body))
    (rule integer-text-of (head (metta-equation
      (tptp-res-v1:integer-text-of (LangDef:IntegerValue ?text)) ?text)) (body))
    ; N/D in lowest terms, as the compact form keeps it.
    (rule rational-lexeme (head (metta-equation
      (tptp-res-v1:rational-lexeme ?numerator ?denominator)
      (str:concat (tptp-res-v1:integer-text ?numerator)
        (str:concat "/" (tptp-res-v1:integer-text ?denominator))))) (body))))
