; Pure include expansion over the compact official TPTP record view.
; File-system lookup is performed before this transformation and supplied as
; an explicit finite environment.  The result preserves record occurrences,
; include order, duplicate expansion, namespace structure, and include edges.
(gslt-presentation-v1 TptpOfficialCompactIncludeResolutionV1
  (signature
    (operator metta-equation 2)
    (operator + 2)
    (operator langdef:ground-equal 2)
    (operator langdef:value-view 1)
    (operator langdef:list->expression 1)
    (operator langdef:list->list-value 1)
    (operator LangDef:SameValue 0)
    (operator LangDef:DifferentValue 0)
    (operator LangDef:ExpressionValue 1)
    (operator LangDef:ListValue 1)
    (operator LNil 0)
    (operator LCons 2)

    (operator tptp-ci-v1:environment 2)
    (operator tptp-ci-v1:environment-documents 1)
    (operator tptp-ci-v1:expand-nested 6)
    (operator TPTP:IncludeDepthExhaustedV1 1)
    (operator tptp-ci-v1:documents-nil 0)
    (operator tptp-ci-v1:documents-cons 2)
    (operator tptp-ci-v1:source-document 4)
    (operator tptp-ci-v1:bindings-nil 0)
    (operator tptp-ci-v1:bindings-cons 2)
    (operator tptp-ci-v1:include-binding 4)
    (operator tptp-ci-v1:sources-nil 0)
    (operator tptp-ci-v1:sources-cons 2)
    (operator tptp-ci-v1:strings-nil 0)
    (operator tptp-ci-v1:strings-cons 2)

    (operator tptp-ci-v1:resolve 2)
    (operator tptp-ci-v1:resolve-result 2)
    (operator tptp-ci-v1:expand-file 6)
    (operator tptp-ci-v1:expand-file-document 7)
    (operator tptp-ci-v1:expand-inputs 9)
    (operator tptp-ci-v1:expand-inputs-view 9)
    (operator tptp-ci-v1:expand-input 11)
    (operator tptp-ci-v1:emit-formula 11)
    (operator tptp-ci-v1:prepend-input 2)
    (operator tptp-ci-v1:expand-include-selection 13)
    (operator tptp-ci-v1:expand-include-namespace 13)
    (operator tptp-ci-v1:expand-include-binding 14)
    (operator tptp-ci-v1:expand-include-cycle 16)
    (operator tptp-ci-v1:after-include-target 15)
    (operator tptp-ci-v1:after-include-selection 15)
    (operator tptp-ci-v1:after-include-tail 3)

    (operator tptp-ci-v1:decode-selection 1)
    (operator tptp-ci-v1:decode-names 1)
    (operator tptp-ci-v1:decode-names-view 1)
    (operator tptp-ci-v1:prepend-string 2)
    (operator tptp-ci-v1:decode-namespace 2)
    (operator tptp-ci-v1:selection-all 0)
    (operator tptp-ci-v1:selection-names 1)
    (operator tptp-ci-v1:namespace-value 1)

    (operator tptp-ci-v1:lookup-document 2)
    (operator tptp-ci-v1:scan-documents 3)
    (operator tptp-ci-v1:scan-document-decision 4)
    (operator tptp-ci-v1:no-document 0)
    (operator tptp-ci-v1:one-document 1)
    (operator tptp-ci-v1:document-ok 1)
    (operator tptp-ci-v1:document-rest 2)
    (operator tptp-ci-v1:lookup-binding 3)
    (operator tptp-ci-v1:scan-bindings 4)
    (operator tptp-ci-v1:binding-key 4)
    (operator tptp-ci-v1:no-binding 0)
    (operator tptp-ci-v1:one-binding 2)
    (operator tptp-ci-v1:binding-ok 2)
    (operator tptp-ci-v1:binding-entry 2)
    (operator tptp-ci-v1:binding-rest 4)

    (operator tptp-ci-v1:source-member 2)
    (operator tptp-ci-v1:source-member-decision 3)
    (operator tptp-ci-v1:true 0)
    (operator tptp-ci-v1:false 0)
    (operator tptp-ci-v1:append-inputs 2)
    (operator tptp-ci-v1:append-edges 2)
    (operator tptp-ci-v1:expanded-inputs-nil 0)
    (operator tptp-ci-v1:expanded-inputs-cons 2)
    (operator tptp-ci-v1:include-edges-nil 0)
    (operator tptp-ci-v1:include-edges-cons 2)
    (operator tptp-ci-v1:ok 2)

    (operator tptp-ci-v1:apply-selection 3)
    (operator tptp-ci-v1:validate-selection 3)
    (operator tptp-ci-v1:validate-selection-names 4)
    (operator tptp-ci-v1:validate-selection-count 6)
    (operator tptp-ci-v1:validate-selection-zero 7)
    (operator tptp-ci-v1:validate-selection-one 7)
    (operator tptp-ci-v1:count-name 2)
    (operator tptp-ci-v1:count-name-decision 3)
    (operator tptp-ci-v1:first-duplicate 1)
    (operator tptp-ci-v1:first-duplicate-decision 3)
    (operator tptp-ci-v1:selection-duplicate-decision 3)
    (operator tptp-ci-v1:string-member 2)
    (operator tptp-ci-v1:string-member-decision 3)
    (operator tptp-ci-v1:filter-selected 2)
    (operator tptp-ci-v1:filter-selected-decision 4)
    (operator tptp-ci-v1:selected 1)
    (operator tptp-ci-v1:selection-validation 2)
    (operator tptp-ci-v1:duplicate 1)

    (operator TPTP:ExpandedDocumentV1 3)
    (operator TPTP:ExpandedInputV1 5)
    (operator TPTP:SourceOccurrenceV1 4)
    (operator TPTP:SpanUnavailable 1)
    (operator TPTP:QualifiedFormulaNameV1 2)
    (operator TPTP:IncludePathRootV1 1)
    (operator TPTP:IncludePathStepV1 3)
    (operator TPTP:NamespaceRootV1 0)
    (operator TPTP:NamespaceChildV1 2)
    (operator TPTP:IncludeEdgeV1 7)
    (operator TPTP:IncludeResolutionFaultV1 1)
    (operator TPTP:IncludeMissingDocumentV1 1)
    (operator TPTP:IncludeAmbiguousDocumentV1 1)
    (operator TPTP:IncludeMissingBindingV1 2)
    (operator TPTP:IncludeAmbiguousBindingV1 2)
    (operator TPTP:IncludeCycleV1 3)
    (operator TPTP:IncludeLexicalFaultV1 2)
    (operator TPTP:IncludeDuplicateSelectionNameV1 2)
    (operator TPTP:IncludeMissingSelectionNameV1 2)
    (operator TPTP:IncludeAmbiguousSelectionNameV1 2))
  (equations)
  (rewrites
    (rule resolve (head (metta-equation
      (tptp-ci-v1:resolve ?root ?environment)
      (tptp-ci-v1:resolve-result ?root
        (tptp-ci-v1:expand-file ?root ?environment
          (tptp-ci-v1:sources-cons ?root (tptp-ci-v1:sources-nil))
          (TPTP:IncludePathRootV1 ?root)
          (TPTP:NamespaceRootV1)
          (tptp-ci-v1:environment-documents ?environment))))) (body))
    (rule resolve-ok (head (metta-equation
      (tptp-ci-v1:resolve-result ?root
        (tptp-ci-v1:ok ?inputs ?edges))
      (TPTP:ExpandedDocumentV1 ?root ?inputs ?edges))) (body))
    (rule resolve-fault (head (metta-equation
      (tptp-ci-v1:resolve-result ?root
        (TPTP:IncludeResolutionFaultV1 ?fault))
      (TPTP:IncludeResolutionFaultV1 ?fault))) (body))

    ; A nested document spends one document of the environment.  The active
    ; path holds distinct documents, so the environment's documents always
    ; outlast the nesting.
    (rule environment-documents (head (metta-equation
      (tptp-ci-v1:environment-documents (tptp-ci-v1:environment ?documents ?bindings))
      ?documents)) (body))
    (rule expand-nested (head (metta-equation
      (tptp-ci-v1:expand-nested (tptp-ci-v1:documents-cons ?spent ?fuel)
        ?source ?environment ?active ?path ?namespace)
      (tptp-ci-v1:expand-file ?source ?environment ?active ?path ?namespace ?fuel))) (body))
    (rule expand-nested-exhausted (head (metta-equation
      (tptp-ci-v1:expand-nested (tptp-ci-v1:documents-nil)
        ?source ?environment ?active ?path ?namespace)
      (TPTP:IncludeResolutionFaultV1 (TPTP:IncludeDepthExhaustedV1 ?source)))) (body))
    (rule expand-file (head (metta-equation
      (tptp-ci-v1:expand-file ?source ?environment ?active ?path ?namespace ?fuel)
      (tptp-ci-v1:expand-file-document ?source ?environment ?active ?path
        ?namespace (tptp-ci-v1:lookup-document ?source ?environment) ?fuel))) (body))
    (rule expand-file-ok (head (metta-equation
      (tptp-ci-v1:expand-file-document ?source ?environment ?active ?path
        ?namespace
        (tptp-ci-v1:document-ok
          (tptp-ci-v1:source-document ?document-source ?digest
            ?inputs ?text)) ?fuel)
      (tptp-ci-v1:expand-inputs ?source ?digest ?inputs ?environment ?active
        ?path ?namespace 0 ?fuel))) (body))
    (rule expand-file-fault (head (metta-equation
      (tptp-ci-v1:expand-file-document ?source ?environment ?active ?path
        ?namespace (TPTP:IncludeResolutionFaultV1 ?fault) ?fuel)
      (TPTP:IncludeResolutionFaultV1 ?fault))) (body))

    (rule inputs (head (metta-equation
      (tptp-ci-v1:expand-inputs ?source ?digest ?inputs
        ?environment ?active ?path ?namespace ?index ?fuel)
      (tptp-ci-v1:expand-inputs-view (langdef:value-view ?inputs)
        ?source ?digest ?environment ?active ?path ?namespace ?index ?fuel))) (body))
    (rule inputs-nil (head (metta-equation
      (tptp-ci-v1:expand-inputs-view (LangDef:ExpressionValue LNil)
        ?source ?digest ?environment ?active ?path ?namespace ?index ?fuel)
      (tptp-ci-v1:ok (tptp-ci-v1:expanded-inputs-nil)
        (tptp-ci-v1:include-edges-nil)))) (body))
    (rule inputs-cons (head (metta-equation
      (tptp-ci-v1:expand-inputs-view
        (LangDef:ExpressionValue (LCons ?record ?tail))
        ?source ?digest ?environment ?active ?path ?namespace ?index ?fuel)
      (tptp-ci-v1:expand-input (langdef:value-view ?record) ?record
        ?source ?digest (langdef:list->expression ?tail)
        ?environment ?active ?path ?namespace ?index ?fuel))) (body))
    ; A document's inputs are a list; the rest stays a list.
    (rule inputs-list-nil (head (metta-equation
      (tptp-ci-v1:expand-inputs-view (LangDef:ListValue LNil)
        ?source ?digest ?environment ?active ?path ?namespace ?index ?fuel)
      (tptp-ci-v1:ok (tptp-ci-v1:expanded-inputs-nil)
        (tptp-ci-v1:include-edges-nil)))) (body))
    (rule inputs-list-cons (head (metta-equation
      (tptp-ci-v1:expand-inputs-view
        (LangDef:ListValue (LCons ?record ?tail))
        ?source ?digest ?environment ?active ?path ?namespace ?index ?fuel)
      (tptp-ci-v1:expand-input (langdef:value-view ?record) ?record
        ?source ?digest (langdef:list->list-value ?tail)
        ?environment ?active ?path ?namespace ?index ?fuel))) (body))

    (rule input-fof (head (metta-equation
      (tptp-ci-v1:expand-input
        (LangDef:ExpressionValue
          (LCons fof (LCons ?name (LCons ?role (LCons ?formula ?tail-fields)))))
        ?record ?source ?digest ?tail ?environment ?active ?path ?namespace ?index ?fuel)
      (tptp-ci-v1:emit-formula ?source ?digest ?index ?path ?namespace ?name
        ?record ?tail ?environment ?active ?fuel))) (body))
    (rule input-cnf (head (metta-equation
      (tptp-ci-v1:expand-input
        (LangDef:ExpressionValue
          (LCons cnf (LCons ?name (LCons ?role (LCons ?formula ?tail-fields)))))
        ?record ?source ?digest ?tail ?environment ?active ?path ?namespace ?index ?fuel)
      (tptp-ci-v1:emit-formula ?source ?digest ?index ?path ?namespace ?name
        ?record ?tail ?environment ?active ?fuel))) (body))
    (rule input-tff (head (metta-equation
      (tptp-ci-v1:expand-input
        (LangDef:ExpressionValue
          (LCons tff (LCons ?name (LCons ?role (LCons ?formula ?tail-fields)))))
        ?record ?source ?digest ?tail ?environment ?active ?path ?namespace ?index ?fuel)
      (tptp-ci-v1:emit-formula ?source ?digest ?index ?path ?namespace ?name
        ?record ?tail ?environment ?active ?fuel))) (body))
    (rule input-thf (head (metta-equation
      (tptp-ci-v1:expand-input
        (LangDef:ExpressionValue
          (LCons thf (LCons ?name (LCons ?role (LCons ?formula ?tail-fields)))))
        ?record ?source ?digest ?tail ?environment ?active ?path ?namespace ?index ?fuel)
      (tptp-ci-v1:emit-formula ?source ?digest ?index ?path ?namespace ?name
        ?record ?tail ?environment ?active ?fuel))) (body))
    (rule input-tcf (head (metta-equation
      (tptp-ci-v1:expand-input
        (LangDef:ExpressionValue
          (LCons tcf (LCons ?name (LCons ?role (LCons ?formula ?tail-fields)))))
        ?record ?source ?digest ?tail ?environment ?active ?path ?namespace ?index ?fuel)
      (tptp-ci-v1:emit-formula ?source ?digest ?index ?path ?namespace ?name
        ?record ?tail ?environment ?active ?fuel))) (body))
    (rule input-tpi (head (metta-equation
      (tptp-ci-v1:expand-input
        (LangDef:ExpressionValue
          (LCons tpi (LCons ?name (LCons ?role (LCons ?formula ?tail-fields)))))
        ?record ?source ?digest ?tail ?environment ?active ?path ?namespace ?index ?fuel)
      (tptp-ci-v1:emit-formula ?source ?digest ?index ?path ?namespace ?name
        ?record ?tail ?environment ?active ?fuel))) (body))

    (rule emit-formula (head (metta-equation
      (tptp-ci-v1:emit-formula ?source ?digest ?index ?path ?namespace ?name
        ?record ?tail ?environment ?active ?fuel)
      (tptp-ci-v1:prepend-input
        (TPTP:ExpandedInputV1
          (TPTP:SourceOccurrenceV1 ?source ?digest ?index
            (TPTP:SpanUnavailable compact-value))
          ?path ?namespace (TPTP:QualifiedFormulaNameV1 ?namespace ?name)
          ?record)
        (tptp-ci-v1:expand-inputs ?source ?digest ?tail ?environment ?active
          ?path ?namespace (+ ?index 1) ?fuel)))) (body))
    (rule prepend-input-ok (head (metta-equation
      (tptp-ci-v1:prepend-input ?input (tptp-ci-v1:ok ?inputs ?edges))
      (tptp-ci-v1:ok
        (tptp-ci-v1:expanded-inputs-cons ?input ?inputs) ?edges))) (body))
    (rule prepend-input-fault (head (metta-equation
      (tptp-ci-v1:prepend-input ?input
        (TPTP:IncludeResolutionFaultV1 ?fault))
      (TPTP:IncludeResolutionFaultV1 ?fault))) (body))

    (rule input-include-all (head (metta-equation
      (tptp-ci-v1:expand-input
        (LangDef:ExpressionValue (LCons incl (LCons ?requested LNil)))
        ?record ?source ?digest ?tail ?environment ?active ?path ?namespace ?index ?fuel)
      (tptp-ci-v1:expand-include-selection
        (tptp-ci-v1:selection-all) ?requested ?source ?digest ?index ?path
        ?namespace (tptp-ci-v1:namespace-value ?namespace)
        (TPTP:SpanUnavailable compact-value) ?tail ?environment ?active ?fuel))) (body))
    (rule input-include-selected (head (metta-equation
      (tptp-ci-v1:expand-input
        (LangDef:ExpressionValue
          (LCons incl (LCons ?requested (LCons ?selection LNil))))
        ?record ?source ?digest ?tail ?environment ?active ?path ?namespace ?index ?fuel)
      (tptp-ci-v1:expand-include-selection
        (tptp-ci-v1:decode-selection ?selection) ?requested ?source ?digest
        ?index ?path ?namespace (tptp-ci-v1:namespace-value ?namespace)
        (TPTP:SpanUnavailable compact-value) ?tail ?environment ?active ?fuel))) (body))
    (rule input-include-qualified (head (metta-equation
      (tptp-ci-v1:expand-input
        (LangDef:ExpressionValue
          (LCons incl
            (LCons ?requested (LCons ?selection (LCons ?qualification LNil)))))
        ?record ?source ?digest ?tail ?environment ?active ?path ?namespace ?index ?fuel)
      (tptp-ci-v1:expand-include-selection
        (tptp-ci-v1:decode-selection ?selection) ?requested ?source ?digest
        ?index ?path ?namespace
        (tptp-ci-v1:decode-namespace ?namespace ?qualification)
        (TPTP:SpanUnavailable compact-value) ?tail ?environment ?active ?fuel))) (body))
    (rule include-selection-all-value (head (metta-equation
      (tptp-ci-v1:expand-include-selection
        (tptp-ci-v1:selection-all) ?requested ?source
        ?digest ?index ?path ?namespace ?qualification ?span ?tail
        ?environment ?active ?fuel)
      (tptp-ci-v1:expand-include-namespace
        ?qualification
        (tptp-ci-v1:selection-all) ?requested ?source ?digest ?index ?path
        ?span ?tail
        ?environment ?active ?namespace ?fuel))) (body))
    (rule include-selection-names-value (head (metta-equation
      (tptp-ci-v1:expand-include-selection
        (tptp-ci-v1:selection-names ?names) ?requested ?source
        ?digest ?index ?path ?namespace ?qualification ?span ?tail
        ?environment ?active ?fuel)
      (tptp-ci-v1:expand-include-namespace
        ?qualification
        (tptp-ci-v1:selection-names ?names) ?requested ?source ?digest
        ?index ?path ?span ?tail
        ?environment ?active ?namespace ?fuel))) (body))
    (rule include-selection-fault (head (metta-equation
      (tptp-ci-v1:expand-include-selection
        (TPTP:IncludeResolutionFaultV1 ?fault) ?requested ?source ?digest
        ?index ?path ?namespace ?qualification ?span ?tail ?environment
        ?active ?fuel)
      (TPTP:IncludeResolutionFaultV1 ?fault))) (body))
    (rule include-namespace-value (head (metta-equation
      (tptp-ci-v1:expand-include-namespace
        (tptp-ci-v1:namespace-value ?next-namespace) ?selection ?requested
        ?source ?digest ?index ?path ?span ?tail ?environment ?active
        ?namespace ?fuel)
      (tptp-ci-v1:expand-include-binding
        (tptp-ci-v1:lookup-binding ?source ?requested ?environment)
        ?selection ?requested ?source ?digest ?index ?path ?span ?tail
        ?environment ?active ?next-namespace ?namespace ?fuel))) (body))
    (rule include-namespace-fault (head (metta-equation
      (tptp-ci-v1:expand-include-namespace
        (TPTP:IncludeResolutionFaultV1 ?fault) ?selection ?requested ?source
        ?digest ?index ?path ?span ?tail ?environment ?active ?namespace ?fuel)
      (TPTP:IncludeResolutionFaultV1 ?fault))) (body))
    (rule include-binding-value (head (metta-equation
      (tptp-ci-v1:expand-include-binding
        (tptp-ci-v1:binding-ok ?target ?mode) ?selection ?requested ?source
        ?digest ?index ?path ?span ?tail ?environment ?active ?namespace
        ?parent-namespace ?fuel)
      (tptp-ci-v1:expand-include-cycle
        (tptp-ci-v1:source-member ?target ?active) ?target ?mode ?selection
        ?requested ?source ?digest ?index ?path ?span ?tail ?environment
        ?active ?namespace ?parent-namespace ?fuel))) (body))
    (rule include-binding-fault (head (metta-equation
      (tptp-ci-v1:expand-include-binding
        (TPTP:IncludeResolutionFaultV1 ?fault) ?selection ?requested ?source
        ?digest ?index ?path ?span ?tail ?environment ?active ?namespace
        ?parent-namespace ?fuel)
      (TPTP:IncludeResolutionFaultV1 ?fault))) (body))
    (rule include-cycle-found (head (metta-equation
      (tptp-ci-v1:expand-include-cycle tptp-ci-v1:true ?target ?mode
        ?selection ?requested ?source ?digest ?index ?path ?span ?tail
        ?environment ?active ?namespace ?parent-namespace ?fuel)
      (TPTP:IncludeResolutionFaultV1
        (TPTP:IncludeCycleV1 ?source ?target ?path)))) (body))
    (rule include-cycle-clear (head (metta-equation
      (tptp-ci-v1:expand-include-cycle tptp-ci-v1:false ?target ?mode
        ?selection ?requested ?source ?digest ?index ?path ?span ?tail
        ?environment ?active ?namespace ?parent-namespace ?fuel)
      (tptp-ci-v1:after-include-target
        (TPTP:IncludeEdgeV1
          (TPTP:SourceOccurrenceV1 ?source ?digest ?index ?span)
          ?requested ?target ?mode ?selection ?namespace ?path)
        ?selection ?target
        (tptp-ci-v1:expand-nested ?fuel ?target ?environment
          (tptp-ci-v1:sources-cons ?target ?active)
          (TPTP:IncludePathStepV1 ?path
            (TPTP:SourceOccurrenceV1 ?source ?digest ?index ?span) ?target)
          ?namespace)
        ?source ?digest ?index ?path ?span ?tail ?environment ?active
        ?namespace ?parent-namespace ?fuel))) (body))
    (rule after-target-ok (head (metta-equation
      (tptp-ci-v1:after-include-target ?edge ?selection ?target
        (tptp-ci-v1:ok ?target-inputs ?target-edges)
        ?source ?digest ?index ?path ?span ?tail ?environment ?active
        ?namespace ?parent-namespace ?fuel)
      (tptp-ci-v1:after-include-selection ?edge ?target ?target-edges
        (tptp-ci-v1:apply-selection ?target ?selection ?target-inputs)
        ?source ?digest ?index ?path ?span ?tail ?environment ?active
        ?namespace ?parent-namespace ?fuel))) (body))
    (rule after-target-fault (head (metta-equation
      (tptp-ci-v1:after-include-target ?edge ?selection ?target
        (TPTP:IncludeResolutionFaultV1 ?fault)
        ?source ?digest ?index ?path ?span ?tail ?environment ?active
        ?namespace ?parent-namespace ?fuel)
      (TPTP:IncludeResolutionFaultV1 ?fault))) (body))
    (rule after-selection-ok (head (metta-equation
      (tptp-ci-v1:after-include-selection ?edge ?target ?target-edges
        (tptp-ci-v1:selected ?selected) ?source ?digest ?index ?path ?span
        ?tail ?environment ?active ?namespace ?parent-namespace ?fuel)
      (tptp-ci-v1:after-include-tail ?edge
        (tptp-ci-v1:ok ?selected ?target-edges)
        (tptp-ci-v1:expand-inputs ?source ?digest ?tail ?environment ?active
          ?path ?parent-namespace (+ ?index 1) ?fuel)))) (body))
    (rule after-selection-fault (head (metta-equation
      (tptp-ci-v1:after-include-selection ?edge ?target ?target-edges
        (TPTP:IncludeResolutionFaultV1 ?fault) ?source ?digest ?index ?path
        ?span ?tail ?environment ?active ?namespace ?parent-namespace ?fuel)
      (TPTP:IncludeResolutionFaultV1 ?fault))) (body))
    (rule after-tail-ok (head (metta-equation
      (tptp-ci-v1:after-include-tail ?edge
        (tptp-ci-v1:ok ?selected ?target-edges)
        (tptp-ci-v1:ok ?tail-inputs ?tail-edges))
      (tptp-ci-v1:ok
        (tptp-ci-v1:append-inputs ?selected ?tail-inputs)
        (tptp-ci-v1:include-edges-cons ?edge
          (tptp-ci-v1:append-edges ?target-edges ?tail-edges))))) (body))
    (rule after-tail-fault (head (metta-equation
      (tptp-ci-v1:after-include-tail ?edge ?target-result
        (TPTP:IncludeResolutionFaultV1 ?fault))
      (TPTP:IncludeResolutionFaultV1 ?fault))) (body))

    (rule selection-names (head (metta-equation
      (tptp-ci-v1:decode-selection ?names)
      (tptp-ci-v1:decode-names ?names))) (body))
    (rule names (head (metta-equation
      (tptp-ci-v1:decode-names ?names)
      (tptp-ci-v1:decode-names-view (langdef:value-view ?names)))) (body))
    (rule names-nil (head (metta-equation
      (tptp-ci-v1:decode-names-view (LangDef:ExpressionValue LNil))
      (tptp-ci-v1:selection-names (tptp-ci-v1:strings-nil)))) (body))
    (rule names-cons (head (metta-equation
      (tptp-ci-v1:decode-names-view
        (LangDef:ExpressionValue (LCons ?head ?tail)))
      (tptp-ci-v1:prepend-string ?head
        (tptp-ci-v1:decode-names-view
          (LangDef:ExpressionValue ?tail))))) (body))
    ; A selection of names is a list.
    (rule names-list-nil (head (metta-equation
      (tptp-ci-v1:decode-names-view (LangDef:ListValue LNil))
      (tptp-ci-v1:selection-names (tptp-ci-v1:strings-nil)))) (body))
    (rule names-list-cons (head (metta-equation
      (tptp-ci-v1:decode-names-view
        (LangDef:ListValue (LCons ?head ?tail)))
      (tptp-ci-v1:prepend-string ?head
        (tptp-ci-v1:decode-names-view
          (LangDef:ListValue ?tail))))) (body))
    (rule prepend-string-ok (head (metta-equation
      (tptp-ci-v1:prepend-string ?head
        (tptp-ci-v1:selection-names ?tail))
      (tptp-ci-v1:selection-names
        (tptp-ci-v1:strings-cons ?head ?tail)))) (body))
    (rule prepend-string-fault (head (metta-equation
      (tptp-ci-v1:prepend-string ?head
        (TPTP:IncludeResolutionFaultV1 ?fault))
      (TPTP:IncludeResolutionFaultV1 ?fault))) (body))
    (rule namespace-value (head (metta-equation
      (tptp-ci-v1:decode-namespace ?namespace ?value)
      (tptp-ci-v1:namespace-value
        (TPTP:NamespaceChildV1 ?namespace ?value)))) (body))

    (rule lookup-document (head (metta-equation
      (tptp-ci-v1:lookup-document ?source
        (tptp-ci-v1:environment ?documents ?bindings))
      (tptp-ci-v1:scan-documents ?source ?documents
        (tptp-ci-v1:no-document)))) (body))
    (rule scan-documents-nil-missing (head (metta-equation
      (tptp-ci-v1:scan-documents ?source (tptp-ci-v1:documents-nil)
        (tptp-ci-v1:no-document))
      (TPTP:IncludeResolutionFaultV1
        (TPTP:IncludeMissingDocumentV1 ?source)))) (body))
    (rule scan-documents-nil-one (head (metta-equation
      (tptp-ci-v1:scan-documents ?source (tptp-ci-v1:documents-nil)
        (tptp-ci-v1:one-document ?document))
      (tptp-ci-v1:document-ok ?document))) (body))
    (rule scan-documents-cons (head (metta-equation
      (tptp-ci-v1:scan-documents ?source
        (tptp-ci-v1:documents-cons
          (tptp-ci-v1:source-document ?candidate ?digest ?records ?text)
          ?tail) ?state)
      (tptp-ci-v1:scan-document-decision
        (langdef:ground-equal ?source ?candidate) ?source
        (tptp-ci-v1:source-document ?candidate ?digest ?records ?text)
        (tptp-ci-v1:document-rest ?tail ?state)))) (body))
    (rule scan-document-different (head (metta-equation
      (tptp-ci-v1:scan-document-decision LangDef:DifferentValue ?source
        ?document (tptp-ci-v1:document-rest ?tail ?state))
      (tptp-ci-v1:scan-documents ?source ?tail ?state))) (body))
    (rule scan-document-first (head (metta-equation
      (tptp-ci-v1:scan-document-decision LangDef:SameValue ?source
        ?document
        (tptp-ci-v1:document-rest ?tail (tptp-ci-v1:no-document)))
      (tptp-ci-v1:scan-documents ?source ?tail
        (tptp-ci-v1:one-document ?document)))) (body))
    (rule scan-document-ambiguous (head (metta-equation
      (tptp-ci-v1:scan-document-decision LangDef:SameValue ?source
        ?document
        (tptp-ci-v1:document-rest ?tail
          (tptp-ci-v1:one-document ?previous)))
      (TPTP:IncludeResolutionFaultV1
        (TPTP:IncludeAmbiguousDocumentV1 ?source)))) (body))

    (rule lookup-binding (head (metta-equation
      (tptp-ci-v1:lookup-binding ?from ?requested
        (tptp-ci-v1:environment ?documents ?bindings))
      (tptp-ci-v1:scan-bindings ?from ?requested ?bindings
        (tptp-ci-v1:no-binding)))) (body))
    (rule scan-bindings-nil-missing (head (metta-equation
      (tptp-ci-v1:scan-bindings ?from ?requested
        (tptp-ci-v1:bindings-nil) (tptp-ci-v1:no-binding))
      (TPTP:IncludeResolutionFaultV1
        (TPTP:IncludeMissingBindingV1 ?from ?requested)))) (body))
    (rule scan-bindings-nil-one (head (metta-equation
      (tptp-ci-v1:scan-bindings ?from ?requested
        (tptp-ci-v1:bindings-nil) (tptp-ci-v1:one-binding ?target ?mode))
      (tptp-ci-v1:binding-ok ?target ?mode))) (body))
    (rule scan-bindings-cons (head (metta-equation
      (tptp-ci-v1:scan-bindings ?from ?requested
        (tptp-ci-v1:bindings-cons
          (tptp-ci-v1:include-binding ?candidate-from ?candidate-requested
            ?target ?mode) ?tail) ?state)
      (tptp-ci-v1:binding-key
        (langdef:ground-equal ?from ?candidate-from)
        (langdef:ground-equal ?requested ?candidate-requested)
        (tptp-ci-v1:binding-entry ?target ?mode)
        (tptp-ci-v1:binding-rest ?from ?requested ?tail ?state)))) (body))
    (rule binding-key-from-different (head (metta-equation
      (tptp-ci-v1:binding-key LangDef:DifferentValue ?requested-decision
        ?entry (tptp-ci-v1:binding-rest ?from ?requested ?tail ?state))
      (tptp-ci-v1:scan-bindings ?from ?requested ?tail ?state))) (body))
    (rule binding-key-requested-different (head (metta-equation
      (tptp-ci-v1:binding-key LangDef:SameValue LangDef:DifferentValue
        ?entry (tptp-ci-v1:binding-rest ?from ?requested ?tail ?state))
      (tptp-ci-v1:scan-bindings ?from ?requested ?tail ?state))) (body))
    (rule binding-key-first (head (metta-equation
      (tptp-ci-v1:binding-key LangDef:SameValue LangDef:SameValue
        (tptp-ci-v1:binding-entry ?target ?mode)
        (tptp-ci-v1:binding-rest ?from ?requested ?tail
          (tptp-ci-v1:no-binding)))
      (tptp-ci-v1:scan-bindings ?from ?requested ?tail
        (tptp-ci-v1:one-binding ?target ?mode)))) (body))
    (rule binding-key-ambiguous (head (metta-equation
      (tptp-ci-v1:binding-key LangDef:SameValue LangDef:SameValue
        ?entry
        (tptp-ci-v1:binding-rest ?from ?requested ?tail
          (tptp-ci-v1:one-binding ?target ?mode)))
      (TPTP:IncludeResolutionFaultV1
        (TPTP:IncludeAmbiguousBindingV1 ?from ?requested)))) (body))

    (rule member-nil (head (metta-equation
      (tptp-ci-v1:source-member ?needle (tptp-ci-v1:sources-nil))
      tptp-ci-v1:false)) (body))
    (rule member-cons (head (metta-equation
      (tptp-ci-v1:source-member ?needle
        (tptp-ci-v1:sources-cons ?head ?tail))
      (tptp-ci-v1:source-member-decision
        (langdef:ground-equal ?needle ?head) ?needle ?tail))) (body))
    (rule member-same (head (metta-equation
      (tptp-ci-v1:source-member-decision LangDef:SameValue ?needle ?tail)
      tptp-ci-v1:true)) (body))
    (rule member-different (head (metta-equation
      (tptp-ci-v1:source-member-decision LangDef:DifferentValue ?needle ?tail)
      (tptp-ci-v1:source-member ?needle ?tail))) (body))

    (rule append-inputs-nil (head (metta-equation
      (tptp-ci-v1:append-inputs (tptp-ci-v1:expanded-inputs-nil) ?right)
      ?right)) (body))
    (rule append-inputs-cons (head (metta-equation
      (tptp-ci-v1:append-inputs
        (tptp-ci-v1:expanded-inputs-cons ?head ?tail) ?right)
      (tptp-ci-v1:expanded-inputs-cons ?head
        (tptp-ci-v1:append-inputs ?tail ?right)))) (body))
    (rule append-edges-nil (head (metta-equation
      (tptp-ci-v1:append-edges (tptp-ci-v1:include-edges-nil) ?right)
      ?right)) (body))
    (rule append-edges-cons (head (metta-equation
      (tptp-ci-v1:append-edges
        (tptp-ci-v1:include-edges-cons ?head ?tail) ?right)
      (tptp-ci-v1:include-edges-cons ?head
        (tptp-ci-v1:append-edges ?tail ?right)))) (body))

    (rule selection-apply-all (head (metta-equation
      (tptp-ci-v1:apply-selection ?target
        (tptp-ci-v1:selection-all) ?inputs)
      (tptp-ci-v1:selected ?inputs))) (body))
    (rule selection-apply-names (head (metta-equation
      (tptp-ci-v1:apply-selection ?target
        (tptp-ci-v1:selection-names ?requested) ?inputs)
      (tptp-ci-v1:validate-selection ?target ?requested ?inputs))) (body))
    (rule validate-selection (head (metta-equation
      (tptp-ci-v1:validate-selection ?target ?requested ?inputs)
      (tptp-ci-v1:selection-duplicate-decision
        (tptp-ci-v1:first-duplicate ?requested) ?target
        (tptp-ci-v1:selection-validation ?requested ?inputs)))) (body))
    (rule duplicate-none (head (metta-equation
      (tptp-ci-v1:selection-duplicate-decision tptp-ci-v1:false ?target
        (tptp-ci-v1:selection-validation ?requested ?inputs))
      (tptp-ci-v1:validate-selection-names
        ?target ?requested ?requested ?inputs))) (body))
    (rule duplicate-found (head (metta-equation
      (tptp-ci-v1:selection-duplicate-decision
        (tptp-ci-v1:duplicate ?name) ?target ?rest)
      (TPTP:IncludeResolutionFaultV1
        (TPTP:IncludeDuplicateSelectionNameV1 ?target ?name)))) (body))

    (rule validate-selection-nil (head (metta-equation
      (tptp-ci-v1:validate-selection-names ?target
        (tptp-ci-v1:strings-nil) ?original ?inputs)
      (tptp-ci-v1:selected
        (tptp-ci-v1:filter-selected ?inputs ?original)))) (body))
    (rule validate-selection-cons (head (metta-equation
      (tptp-ci-v1:validate-selection-names ?target
        (tptp-ci-v1:strings-cons ?name ?tail) ?original ?inputs)
      (tptp-ci-v1:validate-selection-count
        (tptp-ci-v1:count-name ?name ?inputs) ?target ?name ?tail ?original
        ?inputs))) (body))
    (rule validate-count (head (metta-equation
      (tptp-ci-v1:validate-selection-count
        ?count ?target ?name ?tail ?original ?inputs)
      (tptp-ci-v1:validate-selection-zero
        (langdef:ground-equal ?count 0) ?count ?target ?name ?tail ?original
        ?inputs))) (body))
    (rule validate-count-zero (head (metta-equation
      (tptp-ci-v1:validate-selection-zero LangDef:SameValue ?count
        ?target ?name ?tail ?original ?inputs)
      (TPTP:IncludeResolutionFaultV1
        (TPTP:IncludeMissingSelectionNameV1 ?target ?name)))) (body))
    (rule validate-count-nonzero (head (metta-equation
      (tptp-ci-v1:validate-selection-zero LangDef:DifferentValue ?count
        ?target ?name ?tail ?original ?inputs)
      (tptp-ci-v1:validate-selection-one
        (langdef:ground-equal ?count 1) ?count ?target ?name ?tail ?original
        ?inputs))) (body))
    (rule validate-count-one (head (metta-equation
      (tptp-ci-v1:validate-selection-one LangDef:SameValue ?count
        ?target ?name ?tail ?original ?inputs)
      (tptp-ci-v1:validate-selection-names
        ?target ?tail ?original ?inputs))) (body))
    (rule validate-count-many (head (metta-equation
      (tptp-ci-v1:validate-selection-one LangDef:DifferentValue ?count
        ?target ?name ?tail ?original ?inputs)
      (TPTP:IncludeResolutionFaultV1
        (TPTP:IncludeAmbiguousSelectionNameV1 ?target ?name)))) (body))

    (rule count-name-nil (head (metta-equation
      (tptp-ci-v1:count-name ?name (tptp-ci-v1:expanded-inputs-nil)) 0)) (body))
    (rule count-name-cons (head (metta-equation
      (tptp-ci-v1:count-name ?name
        (tptp-ci-v1:expanded-inputs-cons
          (TPTP:ExpandedInputV1 ?origin ?path ?namespace
            (TPTP:QualifiedFormulaNameV1 ?qualified ?actual) ?record)
          ?tail))
      (tptp-ci-v1:count-name-decision
        (langdef:ground-equal ?name ?actual) ?name ?tail))) (body))
    (rule count-name-same (head (metta-equation
      (tptp-ci-v1:count-name-decision LangDef:SameValue ?name ?tail)
      (+ 1 (tptp-ci-v1:count-name ?name ?tail)))) (body))
    (rule count-name-different (head (metta-equation
      (tptp-ci-v1:count-name-decision LangDef:DifferentValue ?name ?tail)
      (tptp-ci-v1:count-name ?name ?tail))) (body))

    (rule duplicate-list-nil (head (metta-equation
      (tptp-ci-v1:first-duplicate (tptp-ci-v1:strings-nil))
      tptp-ci-v1:false)) (body))
    (rule duplicate-list-cons (head (metta-equation
      (tptp-ci-v1:first-duplicate
        (tptp-ci-v1:strings-cons ?head ?tail))
      (tptp-ci-v1:first-duplicate-decision
        (tptp-ci-v1:string-member ?head ?tail) ?head ?tail))) (body))
    (rule duplicate-list-found (head (metta-equation
      (tptp-ci-v1:first-duplicate-decision tptp-ci-v1:true ?head ?tail)
      (tptp-ci-v1:duplicate ?head))) (body))
    (rule duplicate-list-continue (head (metta-equation
      (tptp-ci-v1:first-duplicate-decision tptp-ci-v1:false ?head ?tail)
      (tptp-ci-v1:first-duplicate ?tail))) (body))
    (rule string-member-nil (head (metta-equation
      (tptp-ci-v1:string-member ?needle (tptp-ci-v1:strings-nil))
      tptp-ci-v1:false)) (body))
    (rule string-member-cons (head (metta-equation
      (tptp-ci-v1:string-member ?needle
        (tptp-ci-v1:strings-cons ?head ?tail))
      (tptp-ci-v1:string-member-decision
        (langdef:ground-equal ?needle ?head) ?needle ?tail))) (body))
    (rule string-member-same (head (metta-equation
      (tptp-ci-v1:string-member-decision LangDef:SameValue ?needle ?tail)
      tptp-ci-v1:true)) (body))
    (rule string-member-different (head (metta-equation
      (tptp-ci-v1:string-member-decision LangDef:DifferentValue ?needle ?tail)
      (tptp-ci-v1:string-member ?needle ?tail))) (body))

    (rule filter-nil (head (metta-equation
      (tptp-ci-v1:filter-selected (tptp-ci-v1:expanded-inputs-nil)
        ?requested)
      (tptp-ci-v1:expanded-inputs-nil))) (body))
    (rule filter-cons (head (metta-equation
      (tptp-ci-v1:filter-selected
        (tptp-ci-v1:expanded-inputs-cons
          (TPTP:ExpandedInputV1 ?origin ?path ?namespace
            (TPTP:QualifiedFormulaNameV1 ?qualified ?name) ?record)
          ?tail) ?requested)
      (tptp-ci-v1:filter-selected-decision
        (tptp-ci-v1:string-member ?name ?requested)
        (TPTP:ExpandedInputV1 ?origin ?path ?namespace
          (TPTP:QualifiedFormulaNameV1 ?qualified ?name) ?record)
        ?tail ?requested))) (body))
    (rule filter-keep (head (metta-equation
      (tptp-ci-v1:filter-selected-decision tptp-ci-v1:true ?head ?tail
        ?requested)
      (tptp-ci-v1:expanded-inputs-cons ?head
        (tptp-ci-v1:filter-selected ?tail ?requested)))) (body))
    (rule filter-skip (head (metta-equation
      (tptp-ci-v1:filter-selected-decision tptp-ci-v1:false ?head ?tail
        ?requested)
      (tptp-ci-v1:filter-selected ?tail ?requested))) (body))))
