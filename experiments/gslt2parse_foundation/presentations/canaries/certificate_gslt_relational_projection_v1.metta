; A seven-table projection guest with opaque table names and no apartness
; relation.  Its active context contains two hypotheses, while one stored
; assertion filters a non-mandatory floating hypothesis from its frame.

(gslt-presentation-v1 ProofGSLTRelationalProjectionCanaryV1
  (signature)
  (equations)
  (rewrites
    (rule relational-projection-canary-extension-v1
      (head
        (source-proof-extension
          RelationalProjectionCanaryProofV1
          RelationalProjectionCanaryLanguageV1))
      (body))
    (rule relational-projection-canary-sequence-layer-v1
      (head
        (source-proof-sequence-layer
          RelationalProjectionCanaryProofV1
          RelationalProjectionCanaryLanguageV1))
      (body))
    (rule relational-projection-canary-layer-v1
      (head
        (source-proof-relational-projection-layer-v1
          RelationalProjectionCanaryProofV1
          RelationalProjectionCanaryLanguageV1))
      (body))

    (rule relational-projection-canary-lifetime-v1
      (head (source-state-lifetime projection-canary-persistent-v1))
      (body))
    (rule relational-projection-canary-table-t0-v1
      (head
        (source-state-table
          projection-canary-t0-v1 2 1 projection-canary-persistent-v1))
      (body))
    (rule relational-projection-canary-table-t1-v1
      (head
        (source-state-table
          projection-canary-t1-v1 2 1 projection-canary-persistent-v1))
      (body))
    (rule relational-projection-canary-table-t2-v1
      (head
        (source-state-table
          projection-canary-t2-v1 2 1 projection-canary-persistent-v1))
      (body))
    (rule relational-projection-canary-table-t3-v1
      (head
        (source-state-table
          projection-canary-t3-v1 3 2 projection-canary-persistent-v1))
      (body))
    (rule relational-projection-canary-table-t4-v1
      (head
        (source-state-table
          projection-canary-t4-v1 2 2 projection-canary-persistent-v1))
      (body))
    (rule relational-projection-canary-table-t5-v1
      (head
        (source-state-table
          projection-canary-t5-v1 2 1 projection-canary-persistent-v1))
      (body))
    (rule relational-projection-canary-table-t6-v1
      (head
        (source-state-table
          projection-canary-t6-v1 3 2 projection-canary-persistent-v1))
      (body))

    (rule relational-projection-canary-role-symbol-v1
      (head
        (source-proof-relational-projection-table-role-v1
          RelationalProjectionCanaryProofV1
          proof-relational-symbol-class-v1 projection-canary-t0-v1))
      (body))
    (rule relational-projection-canary-role-formula-v1
      (head
        (source-proof-relational-projection-table-role-v1
          RelationalProjectionCanaryProofV1
          proof-relational-formula-v1 projection-canary-t1-v1))
      (body))
    (rule relational-projection-canary-role-floating-v1
      (head
        (source-proof-relational-projection-table-role-v1
          RelationalProjectionCanaryProofV1
          proof-relational-floating-binding-v1 projection-canary-t2-v1))
      (body))
    (rule relational-projection-canary-role-ordered-v1
      (head
        (source-proof-relational-projection-table-role-v1
          RelationalProjectionCanaryProofV1
          proof-relational-ordered-context-v1 projection-canary-t3-v1))
      (body))
    (rule relational-projection-canary-role-ordered-frame-v1
      (head
        (source-proof-relational-projection-table-role-v1
          RelationalProjectionCanaryProofV1
          proof-relational-ordered-frame-v1 projection-canary-t6-v1))
      (body))
    (rule relational-projection-canary-role-mandatory-v1
      (head
        (source-proof-relational-projection-table-role-v1
          RelationalProjectionCanaryProofV1
          proof-relational-mandatory-variable-v1 projection-canary-t4-v1))
      (body))
    (rule relational-projection-canary-role-label-v1
      (head
        (source-proof-relational-projection-table-role-v1
          RelationalProjectionCanaryProofV1
          proof-relational-label-class-v1 projection-canary-t5-v1))
      (body))

    (rule relational-projection-canary-literal-token-v1
      (head (source-state-literal projection-canary-token-literal-v1))
      (body))
    (rule relational-projection-canary-variable-token-v1
      (head (source-state-literal projection-canary-token-variable-v1))
      (body))
    (rule relational-projection-canary-binding-label-v1
      (head (source-state-literal projection-canary-label-binding-v1))
      (body))
    (rule relational-projection-canary-essential-label-v1
      (head (source-state-literal projection-canary-label-essential-v1))
      (body))
    (rule relational-projection-canary-assertion-label-v1
      (head (source-state-literal projection-canary-label-assertion-v1))
      (body))
    (rule relational-projection-canary-selector-literal-v1
      (head
        (source-proof-relational-projection-selector-role-v1
          RelationalProjectionCanaryProofV1
          proof-relational-token-literal-v1
          projection-canary-token-literal-v1))
      (body))
    (rule relational-projection-canary-selector-variable-v1
      (head
        (source-proof-relational-projection-selector-role-v1
          RelationalProjectionCanaryProofV1
          proof-relational-token-variable-v1
          projection-canary-token-variable-v1))
      (body))
    (rule relational-projection-canary-selector-binding-v1
      (head
        (source-proof-relational-projection-selector-role-v1
          RelationalProjectionCanaryProofV1
          proof-relational-hypothesis-binding-v1
          projection-canary-label-binding-v1))
      (body))
    (rule relational-projection-canary-selector-essential-v1
      (head
        (source-proof-relational-projection-selector-role-v1
          RelationalProjectionCanaryProofV1
          proof-relational-hypothesis-essential-v1
          projection-canary-label-essential-v1))
      (body))
    (rule relational-projection-canary-selector-assertion-v1
      (head
        (source-proof-relational-projection-selector-role-v1
          RelationalProjectionCanaryProofV1
          proof-relational-label-assertion-v1
          projection-canary-label-assertion-v1))
      (body))

    (rule relational-projection-canary-symbol-wff-v1
      (head
        (source-proof-relational-row2-v1
          projection-canary-t0-v1 projection-canary-wff-v1
          projection-canary-token-literal-v1))
      (body))
    (rule relational-projection-canary-symbol-a-v1
      (head
        (source-proof-relational-row2-v1
          projection-canary-t0-v1 projection-canary-a-v1
          projection-canary-token-literal-v1))
      (body))
    (rule relational-projection-canary-symbol-dummy-v1
      (head
        (source-proof-relational-row2-v1
          projection-canary-t0-v1 projection-canary-dummy-v1
          projection-canary-token-literal-v1))
      (body))
    (rule relational-projection-canary-symbol-x-v1
      (head
        (source-proof-relational-row2-v1
          projection-canary-t0-v1 projection-canary-x-v1
          projection-canary-token-variable-v1))
      (body))
    (rule relational-projection-canary-symbol-y-v1
      (head
        (source-proof-relational-row2-v1
          projection-canary-t0-v1 projection-canary-y-v1
          projection-canary-token-variable-v1))
      (body))

    (rule relational-projection-canary-formula-active-a-v1
      (head
        (source-proof-relational-row2-v1
          projection-canary-t1-v1 projection-canary-ha-v1
          projection-canary-formula-a-v1))
      (body))
    (rule relational-projection-canary-formula-active-dummy-v1
      (head
        (source-proof-relational-row2-v1
          projection-canary-t1-v1 projection-canary-hdummy-v1
          projection-canary-formula-dummy-v1))
      (body))
    (rule relational-projection-canary-formula-x-v1
      (head
        (source-proof-relational-row2-v1
          projection-canary-t1-v1 projection-canary-hx-v1
          projection-canary-formula-x-v1))
      (body))
    (rule relational-projection-canary-formula-y-v1
      (head
        (source-proof-relational-row2-v1
          projection-canary-t1-v1 projection-canary-hy-v1
          projection-canary-formula-y-v1))
      (body))
    (rule relational-projection-canary-formula-identity-v1
      (head
        (source-proof-relational-row2-v1
          projection-canary-t1-v1 projection-canary-identity-v1
          projection-canary-formula-x-v1))
      (body))

    (rule relational-projection-canary-floating-x-v1
      (head
        (source-proof-relational-row2-v1
          projection-canary-t2-v1 projection-canary-hx-v1
          projection-canary-x-v1))
      (body))
    (rule relational-projection-canary-floating-y-v1
      (head
        (source-proof-relational-row2-v1
          projection-canary-t2-v1 projection-canary-hy-v1
          projection-canary-y-v1))
      (body))

    (rule relational-projection-canary-order-active-a-v1
      (head
        (source-proof-relational-row3-v1
          projection-canary-t3-v1 projection-canary-target-v1
          projection-canary-position-0-v1 projection-canary-ha-v1))
      (body))
    (rule relational-projection-canary-order-active-dummy-v1
      (head
        (source-proof-relational-row3-v1
          projection-canary-t3-v1 projection-canary-target-v1
          projection-canary-position-1-v1 projection-canary-hdummy-v1))
      (body))
    (rule relational-projection-canary-order-identity-x-v1
      (head
        (source-proof-relational-row3-v1
          projection-canary-t3-v1 projection-canary-identity-v1
          projection-canary-position-0-v1 projection-canary-hx-v1))
      (body))
    (rule relational-projection-canary-order-identity-y-v1
      (head
        (source-proof-relational-row3-v1
          projection-canary-t3-v1 projection-canary-identity-v1
          projection-canary-position-1-v1 projection-canary-hy-v1))
      (body))
    (rule relational-projection-canary-frame-identity-x-v1
      (head
        (source-proof-relational-row3-v1
          projection-canary-t6-v1 projection-canary-identity-v1
          projection-canary-position-0-v1 projection-canary-hx-v1))
      (body))

    (rule relational-projection-canary-mandatory-x-v1
      (head
        (source-proof-relational-row2-v1
          projection-canary-t4-v1 projection-canary-identity-v1
          projection-canary-x-v1))
      (body))

    (rule relational-projection-canary-label-active-a-v1
      (head
        (source-proof-relational-row2-v1
          projection-canary-t5-v1 projection-canary-ha-v1
          projection-canary-label-essential-v1))
      (body))
    (rule relational-projection-canary-label-active-dummy-v1
      (head
        (source-proof-relational-row2-v1
          projection-canary-t5-v1 projection-canary-hdummy-v1
          projection-canary-label-essential-v1))
      (body))
    (rule relational-projection-canary-label-x-v1
      (head
        (source-proof-relational-row2-v1
          projection-canary-t5-v1 projection-canary-hx-v1
          projection-canary-label-binding-v1))
      (body))
    (rule relational-projection-canary-label-y-v1
      (head
        (source-proof-relational-row2-v1
          projection-canary-t5-v1 projection-canary-hy-v1
          projection-canary-label-binding-v1))
      (body))
    (rule relational-projection-canary-label-identity-v1
      (head
        (source-proof-relational-row2-v1
          projection-canary-t5-v1 projection-canary-identity-v1
          projection-canary-label-assertion-v1))
      (body))

    (rule relational-projection-canary-value-a-v1
      (head
        (source-proof-relational-value-list-v1
          RelationalProjectionCanaryProofV1 projection-canary-formula-a-v1
          (ProofRelationalValueListConsV1 projection-canary-wff-v1
            (ProofRelationalValueListConsV1 projection-canary-a-v1
              ProofRelationalValueListNilV1))))
      (body))
    (rule relational-projection-canary-value-dummy-v1
      (head
        (source-proof-relational-value-list-v1
          RelationalProjectionCanaryProofV1
          projection-canary-formula-dummy-v1
          (ProofRelationalValueListConsV1 projection-canary-wff-v1
            (ProofRelationalValueListConsV1 projection-canary-dummy-v1
              ProofRelationalValueListNilV1))))
      (body))
    (rule relational-projection-canary-value-x-v1
      (head
        (source-proof-relational-value-list-v1
          RelationalProjectionCanaryProofV1 projection-canary-formula-x-v1
          (ProofRelationalValueListConsV1 projection-canary-wff-v1
            (ProofRelationalValueListConsV1 projection-canary-x-v1
              ProofRelationalValueListNilV1))))
      (body))
    (rule relational-projection-canary-value-y-v1
      (head
        (source-proof-relational-value-list-v1
          RelationalProjectionCanaryProofV1 projection-canary-formula-y-v1
          (ProofRelationalValueListConsV1 projection-canary-wff-v1
            (ProofRelationalValueListConsV1 projection-canary-y-v1
              ProofRelationalValueListNilV1))))
      (body))

    (rule relational-projection-canary-request-v1
      (head
        (source-proof-relational-request-v1
          RelationalProjectionCanaryProofV1 projection-canary-request-v1
          projection-canary-target-v1 projection-canary-formula-a-v1))
      (body))
    (rule relational-projection-canary-different-v1
      (head
        (source-proof-relational-value-different-v1
          projection-canary-x-v1 projection-canary-y-v1))
      (body))
  ))
