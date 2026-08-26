; A compositional bridge from a generated relational state program to the
; universal sequence-assertion calculus.  Guest languages assign their state
; relations and selector literals to these roles.  Shapes and role inventory
; are fixed here so the native consumer receives only an admitted ABI.

(gslt-presentation-v1 ProofGSLTRelationalAssertionV1
  (signature
    (operator proof-sequence-relational-layer-valid 2)
    (operator proof-sequence-relational-selector-kind 1)
    (operator proof-sequence-relational-execution-valid 16)
    (operator proof-sequence-relational-table-presence 2)
    (operator proof-sequence-relational-table-policy-valid 3)
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

    ; The public proof executor consumes this generated descriptor rather than
    ; the state machine's fixed table tuple.  The tuple is opaque here: only
    ; execution identity, incomplete-proof policy, and decoder parameters are
    ; projected from the independently admitted source machine.
    (rule proof-sequence-relational-execution
      (head
        (proof-sequence-relational-execution-valid
          ?owner ?machine ?unknown-token
          ?terminal-low ?terminal-high
          ?continuation-low ?continuation-high
          ?save-byte ?unknown-byte
          ?terminal-radix ?terminal-bias
          ?continuation-radix ?continuation-bias
          ?unknown-policy ?save-placement ?header-policy))
      (body
        (proof-sequence-relational-layer-valid ?owner ?base)
        (source-state-proof-machine
          ?machine
          (state-stack-proof-machine-v1
            ?tables
            (state-proof-kinds-v1
              ?binder-kind ?matching-kind ?rule-kind-first ?rule-kind-second
              ?variable-kind ?unknown-token)
            (state-proof-decoder-v1
              ?terminal-low ?terminal-high
              ?continuation-low ?continuation-high
              ?save-byte ?unknown-byte
              ?terminal-radix ?terminal-bias
              ?continuation-radix ?continuation-bias
              ?unknown-policy ?save-placement ?header-policy)))
        (state-proof-machine-valid
          (state-stack-proof-machine-v1
            ?tables
            (state-proof-kinds-v1
              ?binder-kind ?matching-kind ?rule-kind-first ?rule-kind-second
              ?variable-kind ?unknown-token)
            (state-proof-decoder-v1
              ?terminal-low ?terminal-high
              ?continuation-low ?continuation-high
              ?save-byte ?unknown-byte
              ?terminal-radix ?terminal-bias
              ?continuation-radix ?continuation-bias
              ?unknown-policy ?save-placement ?header-policy)))))

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
    (rule proof-sequence-relational-table-active-apartness
      (head
        (proof-sequence-relational-table-shape
          active-apartness-v1 2 2))
      (body))
    (rule proof-sequence-relational-table-label-kind
      (head
        (proof-sequence-relational-table-shape label-kind-v1 2 1))
      (body))

    (rule proof-sequence-relational-table-symbol-kind-required
      (head (proof-sequence-relational-table-presence
        symbol-kind-v1 required-v1))
      (body))
    (rule proof-sequence-relational-table-formula-required
      (head (proof-sequence-relational-table-presence
        formula-v1 required-v1))
      (body))
    (rule proof-sequence-relational-table-floating-variable-required
      (head (proof-sequence-relational-table-presence
        floating-variable-v1 required-v1))
      (body))
    (rule proof-sequence-relational-table-active-hypothesis-required
      (head (proof-sequence-relational-table-presence
        assertion-active-hypothesis-v1 required-v1))
      (body))
    (rule proof-sequence-relational-table-ordered-hypothesis-required
      (head (proof-sequence-relational-table-presence
        ordered-hypothesis-v1 required-v1))
      (body))
    (rule proof-sequence-relational-table-mandatory-variable-required
      (head (proof-sequence-relational-table-presence
        mandatory-variable-v1 required-v1))
      (body))
    (rule proof-sequence-relational-table-assertion-disjoint-optional
      (head (proof-sequence-relational-table-presence
        assertion-disjoint-v1 optional-empty-v1))
      (body))
    (rule proof-sequence-relational-table-active-apartness-optional
      (head (proof-sequence-relational-table-presence
        active-apartness-v1 optional-empty-v1))
      (body))
    (rule proof-sequence-relational-table-label-kind-required
      (head (proof-sequence-relational-table-presence
        label-kind-v1 required-v1))
      (body))

    (rule proof-sequence-relational-table-policy
      (head
        (proof-sequence-relational-table-policy-valid
          ?owner ?role ?presence))
      (body
        (proof-sequence-relational-layer-valid ?owner ?base)
        (proof-sequence-relational-table-shape ?role ?arity ?key-arity)
        (proof-sequence-relational-table-presence ?role ?presence)))

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
