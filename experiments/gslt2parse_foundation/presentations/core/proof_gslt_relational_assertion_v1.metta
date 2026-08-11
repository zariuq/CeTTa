; A compositional bridge from a generated relational state program to the
; universal sequence-assertion calculus.  Guest languages assign their state
; relations and selector literals to these roles.  Shapes and role inventory
; are fixed here so the native consumer receives only an admitted ABI.

(gslt-presentation-v1 ProofGSLTRelationalAssertionV1
  (signature
    (operator proof-sequence-relational-layer-valid 2)
    (operator proof-sequence-relational-selector-kind 1)
    (operator proof-sequence-relational-table-shape 3)
    (operator proof-sequence-relational-table-role-valid 5)
    (operator proof-sequence-relational-selector-role-valid 3)
    (operator source-proof-sequence-relational-layer 2)
    (operator source-proof-sequence-relational-selector-role 3)
    (operator source-proof-sequence-relational-table-role 3)
  )
  (equations)
  (rewrites
    (rule proof-sequence-relational-layer
      (head (proof-sequence-relational-layer-valid ?owner ?base))
      (body
        (source-proof-sequence-relational-layer ?owner ?base)
        (source-proof-extension ?owner ?base)
        (source-proof-sequence-layer ?owner ?base)
        (source-proof-sequence-assertion-layer
          ?owner ?base ?provable)))

    (rule proof-sequence-relational-table-symbol-kind
      (head
        (proof-sequence-relational-table-shape symbol-kind-v1 2 1))
      (body))
    (rule proof-sequence-relational-table-formula
      (head
        (proof-sequence-relational-table-shape formula-v1 2 1))
      (body))
    (rule proof-sequence-relational-table-floating-variable
      (head
        (proof-sequence-relational-table-shape
          floating-variable-v1 2 1))
      (body))
    (rule proof-sequence-relational-table-assertion-active-hypothesis
      (head
        (proof-sequence-relational-table-shape
          assertion-active-hypothesis-v1 2 2))
      (body))
    (rule proof-sequence-relational-table-ordered-hypothesis
      (head
        (proof-sequence-relational-table-shape
          ordered-hypothesis-v1 3 2))
      (body))
    (rule proof-sequence-relational-table-mandatory-variable
      (head
        (proof-sequence-relational-table-shape
          mandatory-variable-v1 2 2))
      (body))
    (rule proof-sequence-relational-table-assertion-disjoint
      (head
        (proof-sequence-relational-table-shape
          assertion-disjoint-v1 3 3))
      (body))
    (rule proof-sequence-relational-table-label-kind
      (head
        (proof-sequence-relational-table-shape label-kind-v1 2 1))
      (body))

    (rule proof-sequence-relational-selector-symbol-literal
      (head
        (proof-sequence-relational-selector-kind symbol-literal-v1))
      (body))
    (rule proof-sequence-relational-selector-symbol-variable
      (head
        (proof-sequence-relational-selector-kind symbol-variable-v1))
      (body))
    (rule proof-sequence-relational-selector-hypothesis-floating
      (head
        (proof-sequence-relational-selector-kind hypothesis-floating-v1))
      (body))
    (rule proof-sequence-relational-selector-hypothesis-essential
      (head
        (proof-sequence-relational-selector-kind hypothesis-essential-v1))
      (body))

    (rule proof-sequence-relational-table-role
      (head
        (proof-sequence-relational-table-role-valid
          ?owner ?role ?table ?arity ?key-arity))
      (body
        (proof-sequence-relational-layer-valid ?owner ?base)
        (source-proof-sequence-relational-table-role
          ?owner ?role ?table)
        (proof-sequence-relational-table-shape
          ?role ?arity ?key-arity)
        (source-state-table ?table ?arity ?key-arity ?lifetime)
        (source-state-lifetime ?lifetime)))

    (rule proof-sequence-relational-selector-role
      (head
        (proof-sequence-relational-selector-role-valid
          ?owner ?role ?literal))
      (body
        (proof-sequence-relational-layer-valid ?owner ?base)
        (source-proof-sequence-relational-selector-role
          ?owner ?role ?literal)
        (proof-sequence-relational-selector-kind ?role)
        (source-state-literal ?literal)))
  ))
