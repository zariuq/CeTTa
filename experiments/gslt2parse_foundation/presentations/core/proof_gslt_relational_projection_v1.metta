; Relational projection from an opaque state store into the chronological
; sequence-proof input ABI.  A guest assigns its tables and selector values
; to the roles below.  Store rows remain extensional facts; all classification,
; mandatory-frame filtering, ordering evidence, and apartness filtering are
; expressed by these rules rather than by a native callback.

(gslt-presentation-v1 ProofGSLTRelationalProjectionV1
  (signature
    (operator ProofRelationalLabelV1 2)
    (operator ProofRelationalProjectionApartnessEntryV1 5)
    (operator ProofRelationalProjectionContextEntryV1 4)
    (operator ProofRelationalProjectionFrameEntryV1 4)
    (operator ProofRelationalRequestV1 2)
    (operator ProofRelationalSequenceV1 3)
    (operator ProofRelationalTokenV1 2)
    (operator ProofRelationalValueListConsV1 2)
    (operator proof-relational-projection-layer-valid-v1 2)
    (operator proof-relational-projection-selector-kind-v1 1)
    (operator proof-relational-projection-selector-role-valid-v1 3)
    (operator proof-relational-projection-table-role-valid-v1 5)
    (operator proof-relational-projection-table-shape-v1 3)
    (operator source-proof-relational-projection-layer-v1 2)
    (operator source-proof-relational-projection-selector-role-v1 3)
    (operator source-proof-relational-projection-table-role-v1 3)
    (operator source-proof-relational-request-v1 4)
    (operator source-proof-relational-row2-v1 3)
    (operator source-proof-relational-row3-v1 4)
    (operator source-proof-relational-row4-v1 5)
    (operator source-proof-relational-value-different-v1 2)
    (operator source-proof-relational-value-list-v1 3)
  )
  (equations)
  (rewrites
    (rule proof-relational-projection-layer-v1
      (head
        (proof-relational-projection-layer-valid-v1 ?owner ?base))
      (body
        (source-proof-relational-projection-layer-v1 ?owner ?base)
        (source-proof-extension ?owner ?base)
        (source-proof-sequence-layer ?owner ?base)))

    (rule proof-relational-projection-table-symbol-class-v1
      (head
        (proof-relational-projection-table-shape-v1
          proof-relational-symbol-class-v1 2 1))
      (body))
    (rule proof-relational-projection-table-formula-v1
      (head
        (proof-relational-projection-table-shape-v1
          proof-relational-formula-v1 2 1))
      (body))
    (rule proof-relational-projection-table-floating-binding-v1
      (head
        (proof-relational-projection-table-shape-v1
          proof-relational-floating-binding-v1 2 1))
      (body))
    (rule proof-relational-projection-table-ordered-context-v1
      (head
        (proof-relational-projection-table-shape-v1
          proof-relational-ordered-context-v1 3 2))
      (body))
    (rule proof-relational-projection-table-ordered-frame-v1
      (head
        (proof-relational-projection-table-shape-v1
          proof-relational-ordered-frame-v1 3 2))
      (body))
    (rule proof-relational-projection-table-mandatory-variable-v1
      (head
        (proof-relational-projection-table-shape-v1
          proof-relational-mandatory-variable-v1 2 2))
      (body))
    (rule proof-relational-projection-table-assertion-apartness-v1
      (head
        (proof-relational-projection-table-shape-v1
          proof-relational-assertion-apartness-v1 4 2))
      (body))
    (rule proof-relational-projection-table-active-apartness-v1
      (head
        (proof-relational-projection-table-shape-v1
          proof-relational-active-apartness-v1 2 2))
      (body))
    (rule proof-relational-projection-table-label-class-v1
      (head
        (proof-relational-projection-table-shape-v1
          proof-relational-label-class-v1 2 1))
      (body))

    (rule proof-relational-projection-selector-token-literal-v1
      (head
        (proof-relational-projection-selector-kind-v1
          proof-relational-token-literal-v1))
      (body))
    (rule proof-relational-projection-selector-token-variable-v1
      (head
        (proof-relational-projection-selector-kind-v1
          proof-relational-token-variable-v1))
      (body))
    (rule proof-relational-projection-selector-hypothesis-binding-v1
      (head
        (proof-relational-projection-selector-kind-v1
          proof-relational-hypothesis-binding-v1))
      (body))
    (rule proof-relational-projection-selector-hypothesis-essential-v1
      (head
        (proof-relational-projection-selector-kind-v1
          proof-relational-hypothesis-essential-v1))
      (body))
    (rule proof-relational-projection-selector-label-assertion-v1
      (head
        (proof-relational-projection-selector-kind-v1
          proof-relational-label-assertion-v1))
      (body))

    (rule proof-relational-projection-table-role-v1
      (head
        (proof-relational-projection-table-role-valid-v1
          ?owner ?role ?table ?arity ?key-arity))
      (body
        (proof-relational-projection-layer-valid-v1 ?owner ?base)
        (source-proof-relational-projection-table-role-v1
          ?owner ?role ?table)
        (proof-relational-projection-table-shape-v1
          ?role ?arity ?key-arity)
        (source-state-table ?table ?arity ?key-arity ?lifetime)
        (source-state-lifetime ?lifetime)))
    (rule proof-relational-projection-selector-role-v1
      (head
        (proof-relational-projection-selector-role-valid-v1
          ?owner ?role ?literal))
      (body
        (proof-relational-projection-layer-valid-v1 ?owner ?base)
        (source-proof-relational-projection-selector-role-v1
          ?owner ?role ?literal)
        (proof-relational-projection-selector-kind-v1 ?role)
        (source-state-literal ?literal)))

    (rule proof-relational-projection-sequence-nil-v1
      (head
        (ProofRelationalSequenceV1
          ?owner ProofRelationalValueListNilV1 ProofSequenceNilV1))
      (body
        (proof-relational-projection-layer-valid-v1 ?owner ?base)))
    (rule proof-relational-projection-sequence-cons-v1
      (head
        (ProofRelationalSequenceV1
          ?owner
          (ProofRelationalValueListConsV1 ?token ?rest)
          (ProofSequenceConsV1
            (ProofRelationalTokenV1 ?owner ?token) ?sequence)))
      (body
        (ProofRelationalSequenceV1 ?owner ?rest ?sequence)))

    (rule proof-relational-projection-token-literal-v1
      (head
        (source-proof-trace-input-token-literal-v1
          (ProofRelationalTokenV1 ?owner ?token)))
      (body
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-symbol-class-v1 ?table 2 1)
        (proof-relational-projection-selector-role-valid-v1
          ?owner proof-relational-token-literal-v1 ?literal)
        (source-proof-relational-row2-v1 ?table ?token ?literal)))
    (rule proof-relational-projection-token-variable-v1
      (head
        (source-proof-trace-input-token-variable-v1
          (ProofRelationalTokenV1 ?owner ?token)))
      (body
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-symbol-class-v1 ?table 2 1)
        (proof-relational-projection-selector-role-valid-v1
          ?owner proof-relational-token-variable-v1 ?variable)
        (source-proof-relational-row2-v1 ?table ?token ?variable)))
    (rule proof-relational-projection-token-different-v1
      (head
        (source-proof-trace-input-token-different-v1
          (ProofRelationalTokenV1 ?owner ?left)
          (ProofRelationalTokenV1 ?owner ?right)))
      (body
        (proof-relational-projection-layer-valid-v1 ?owner ?base)
        (source-proof-relational-value-different-v1 ?left ?right)))
    (rule proof-relational-projection-label-different-v1
      (head
        (source-proof-trace-input-label-different-v1
          (ProofRelationalLabelV1 ?owner ?left)
          (ProofRelationalLabelV1 ?owner ?right)))
      (body
        (proof-relational-projection-layer-valid-v1 ?owner ?base)
        (source-proof-relational-value-different-v1 ?left ?right)))

    (rule proof-relational-projection-request-v1
      (head
        (source-proof-trace-input-request-v1
          (ProofRelationalRequestV1 ?owner ?request) ?target))
      (body
        (source-proof-relational-request-v1
          ?owner ?request ?assertion ?target-value)
        (source-proof-relational-value-list-v1
          ?owner ?target-value ?target-list)
        (ProofRelationalSequenceV1 ?owner ?target-list ?target)))

    (rule proof-relational-projection-context-entry-v1
      (head
        (ProofRelationalProjectionContextEntryV1
          ?owner ?request ?position ?formula))
      (body
        (source-proof-relational-request-v1
          ?owner ?request ?assertion ?target-value)
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-ordered-context-v1 ?ordered 3 2)
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-formula-v1 ?formulas 2 1)
        (source-proof-relational-row3-v1
          ?ordered ?assertion ?position ?label)
        (source-proof-relational-row2-v1
          ?formulas ?label ?formula-value)
        (source-proof-relational-value-list-v1
          ?owner ?formula-value ?formula-list)
        (ProofRelationalSequenceV1 ?owner ?formula-list ?formula)))
    (rule proof-relational-projection-context-export-v1
      (head
        (source-proof-trace-input-context-v1
          (ProofRelationalRequestV1 ?owner ?request)
          ?position ?formula))
      (body
        (ProofRelationalProjectionContextEntryV1
          ?owner ?request ?position ?formula)))
    (rule proof-relational-projection-push-label-v1
      (head
        (source-proof-trace-input-push-label-v1
          (ProofRelationalRequestV1 ?owner ?request)
          (ProofRelationalLabelV1 ?owner ?label) ?formula))
      (body
        (source-proof-relational-request-v1
          ?owner ?request ?assertion ?target-value)
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-ordered-context-v1 ?ordered 3 2)
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-formula-v1 ?formulas 2 1)
        (source-proof-relational-row3-v1
          ?ordered ?assertion ?position ?label)
        (source-proof-relational-row2-v1
          ?formulas ?label ?formula-value)
        (source-proof-relational-value-list-v1
          ?owner ?formula-value ?formula-list)
        (ProofRelationalSequenceV1 ?owner ?formula-list ?formula)))

    (rule proof-relational-projection-frame-binding-v1
      (head
        (ProofRelationalProjectionFrameEntryV1
          ?owner (ProofRelationalLabelV1 ?owner ?assertion) ?position
          (ProofTraceFrameVariableV1
            (ProofRelationalTokenV1 ?owner ?variable)
            (ProofRelationalTokenV1 ?owner ?typecode))))
      (body
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-ordered-frame-v1 ?ordered 3 2)
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-label-class-v1 ?classes 2 1)
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-floating-binding-v1 ?bindings 2 1)
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-mandatory-variable-v1 ?mandatory 2 2)
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-formula-v1 ?formulas 2 1)
        (proof-relational-projection-selector-role-valid-v1
          ?owner proof-relational-hypothesis-binding-v1 ?binding-class)
        (source-proof-relational-row3-v1
          ?ordered ?assertion ?position ?label)
        (source-proof-relational-row2-v1
          ?classes ?label ?binding-class)
        (source-proof-relational-row2-v1
          ?bindings ?label ?variable)
        (source-proof-relational-row2-v1
          ?mandatory ?assertion ?variable)
        (source-proof-relational-row2-v1
          ?formulas ?label ?formula-value)
        (source-proof-relational-value-list-v1
          ?owner ?formula-value
          (ProofRelationalValueListConsV1
            ?typecode
            (ProofRelationalValueListConsV1
              ?variable ProofRelationalValueListNilV1)))))
    (rule proof-relational-projection-frame-essential-v1
      (head
        (ProofRelationalProjectionFrameEntryV1
          ?owner (ProofRelationalLabelV1 ?owner ?assertion) ?position
          (ProofTraceFrameEssentialV1 ?formula)))
      (body
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-ordered-frame-v1 ?ordered 3 2)
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-label-class-v1 ?classes 2 1)
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-formula-v1 ?formulas 2 1)
        (proof-relational-projection-selector-role-valid-v1
          ?owner proof-relational-hypothesis-essential-v1 ?essential-class)
        (source-proof-relational-row3-v1
          ?ordered ?assertion ?position ?label)
        (source-proof-relational-row2-v1
          ?classes ?label ?essential-class)
        (source-proof-relational-row2-v1
          ?formulas ?label ?formula-value)
        (source-proof-relational-value-list-v1
          ?owner ?formula-value ?formula-list)
        (ProofRelationalSequenceV1 ?owner ?formula-list ?formula)))
    (rule proof-relational-projection-frame-export-v1
      (head
        (source-proof-trace-input-frame-v1
          ?label ?position ?entry))
      (body
        (ProofRelationalProjectionFrameEntryV1
          ?owner ?label ?position ?entry)))

    (rule proof-relational-projection-assertion-v1
      (head
        (source-proof-trace-input-assertion-v1
          (ProofRelationalLabelV1 ?owner ?assertion)
          (ProofRelationalTokenV1 ?owner ?typecode) ?body))
      (body
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-label-class-v1 ?classes 2 1)
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-formula-v1 ?formulas 2 1)
        (proof-relational-projection-selector-role-valid-v1
          ?owner proof-relational-label-assertion-v1 ?assertion-class)
        (source-proof-relational-row2-v1
          ?classes ?assertion ?assertion-class)
        (source-proof-relational-row2-v1
          ?formulas ?assertion ?formula-value)
        (source-proof-relational-value-list-v1
          ?owner ?formula-value
          (ProofRelationalValueListConsV1 ?typecode ?body-list))
        (ProofRelationalSequenceV1 ?owner ?body-list ?body)))

    (rule proof-relational-projection-assertion-apartness-v1
      (head
        (ProofRelationalProjectionApartnessEntryV1
          ?owner (ProofRelationalLabelV1 ?owner ?assertion)
          ?position
          (ProofRelationalTokenV1 ?owner ?left)
          (ProofRelationalTokenV1 ?owner ?right)))
      (body
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-assertion-apartness-v1 ?apartness 4 2)
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-mandatory-variable-v1 ?mandatory 2 2)
        (source-proof-relational-row4-v1
          ?apartness ?assertion ?position ?left ?right)
        (source-proof-relational-row2-v1
          ?mandatory ?assertion ?left)
        (source-proof-relational-row2-v1
          ?mandatory ?assertion ?right)))
    (rule proof-relational-projection-assertion-apartness-export-v1
      (head
        (source-proof-trace-input-apartness-v1
          ?label ?position ?left ?right))
      (body
        (ProofRelationalProjectionApartnessEntryV1
          ?owner ?label ?position ?left ?right)))
    (rule proof-relational-projection-active-apartness-v1
      (head
        (source-proof-trace-input-current-apart-v1
          (ProofRelationalTokenV1 ?owner ?left)
          (ProofRelationalTokenV1 ?owner ?right)))
      (body
        (source-proof-relational-request-v1
          ?owner ?request ?assertion ?target-value)
        (proof-relational-projection-table-role-valid-v1
          ?owner proof-relational-active-apartness-v1 ?apartness 2 2)
        (source-proof-relational-row2-v1 ?apartness ?left ?right)))
  ))
