; Metamath's generated source-state relations instantiate the generic
; relational assertion ABI.  This file assigns names only; table shapes,
; completeness, and selector admission are checked by the generic GSLT.

(gslt-presentation-v1 MetamathProofRelationalBridgeV1
  (signature)
  (equations)
  (rewrites
    (rule mm-proof-relational-layer
      (head
        (source-proof-sequence-relational-layer
          MetamathProofV1 MetamathLanguageV1))
      (body))

    (rule mm-proof-relational-table-symbol-kind
      (head
        (source-proof-sequence-relational-table-role
          MetamathProofV1 symbol-kind-v1 mm-state-symbol-kind))
      (body))
    (rule mm-proof-relational-table-formula
      (head
        (source-proof-sequence-relational-table-role
          MetamathProofV1 formula-v1 mm-state-formula))
      (body))
    (rule mm-proof-relational-table-floating-variable
      (head
        (source-proof-sequence-relational-table-role
          MetamathProofV1 floating-variable-v1
          mm-state-floating-variable))
      (body))
    (rule mm-proof-relational-table-assertion-active-hypothesis
      (head
        (source-proof-sequence-relational-table-role
          MetamathProofV1 assertion-active-hypothesis-v1
          mm-state-assertion-active-hypothesis))
      (body))
    (rule mm-proof-relational-table-ordered-hypothesis
      (head
        (source-proof-sequence-relational-table-role
          MetamathProofV1 ordered-hypothesis-v1
          mm-state-assertion-ordered-hypothesis))
      (body))
    (rule mm-proof-relational-table-mandatory-variable
      (head
        (source-proof-sequence-relational-table-role
          MetamathProofV1 mandatory-variable-v1
          mm-state-assertion-mandatory-variable))
      (body))
    (rule mm-proof-relational-table-assertion-disjoint
      (head
        (source-proof-sequence-relational-table-role
          MetamathProofV1 assertion-disjoint-v1
          mm-state-assertion-disjoint-variable))
      (body))
    (rule mm-proof-relational-table-active-apartness
      (head
        (source-proof-sequence-relational-table-role
          MetamathProofV1 active-apartness-v1
          mm-state-disjoint-variable))
      (body))
    (rule mm-proof-relational-table-label-kind
      (head
        (source-proof-sequence-relational-table-role
          MetamathProofV1 label-kind-v1 mm-state-label-kind))
      (body))

    (rule mm-proof-relational-selector-symbol-literal
      (head
        (source-proof-sequence-relational-selector-role
          MetamathProofV1 symbol-literal-v1 mm-symbol-constant))
      (body))
    (rule mm-proof-relational-selector-symbol-variable
      (head
        (source-proof-sequence-relational-selector-role
          MetamathProofV1 symbol-variable-v1 mm-symbol-variable))
      (body))
    (rule mm-proof-relational-selector-hypothesis-floating
      (head
        (source-proof-sequence-relational-selector-role
          MetamathProofV1 hypothesis-floating-v1 mm-label-floating))
      (body))
    (rule mm-proof-relational-selector-hypothesis-essential
      (head
        (source-proof-sequence-relational-selector-role
          MetamathProofV1 hypothesis-essential-v1 mm-label-essential))
      (body))
  ))
