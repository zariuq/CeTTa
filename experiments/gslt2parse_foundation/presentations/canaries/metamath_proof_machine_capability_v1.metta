; A small state-shaped capability for the complete generated proof program.
; Low-level relational rows drive request, token, assertion, and push-label
; projection.  Ordered counts and entries model the generated cursor supplied
; by the state fold.  The active context has two formulas while the stored
; identity assertion has one mandatory frame entry and no apartness entries.

(gslt-presentation-v1 MetamathProofMachineCapabilityCanaryV1
  (signature)
  (equations)
  (rewrites
    (rule mm-proof-capability-symbol-wff-v1
      (head
        (source-proof-relational-row2-v1
          mm-state-symbol-kind capability-wff-v1 mm-symbol-constant))
      (body))
    (rule mm-proof-capability-symbol-a-v1
      (head
        (source-proof-relational-row2-v1
          mm-state-symbol-kind capability-a-v1 mm-symbol-constant))
      (body))
    (rule mm-proof-capability-symbol-dummy-v1
      (head
        (source-proof-relational-row2-v1
          mm-state-symbol-kind capability-dummy-v1 mm-symbol-constant))
      (body))
    (rule mm-proof-capability-symbol-x-v1
      (head
        (source-proof-relational-row2-v1
          mm-state-symbol-kind capability-x-v1 mm-symbol-variable))
      (body))

    (rule mm-proof-capability-formula-active-a-v1
      (head
        (source-proof-relational-row2-v1
          mm-state-formula capability-ha-v1 capability-formula-a-v1))
      (body))
    (rule mm-proof-capability-formula-active-dummy-v1
      (head
        (source-proof-relational-row2-v1
          mm-state-formula capability-hdummy-v1
          capability-formula-dummy-v1))
      (body))
    (rule mm-proof-capability-formula-x-v1
      (head
        (source-proof-relational-row2-v1
          mm-state-formula capability-hx-v1 capability-formula-x-v1))
      (body))
    (rule mm-proof-capability-formula-identity-v1
      (head
        (source-proof-relational-row2-v1
          mm-state-formula capability-identity-v1 capability-formula-x-v1))
      (body))

    (rule mm-proof-capability-floating-x-v1
      (head
        (source-proof-relational-row2-v1
          mm-state-floating-variable capability-hx-v1 capability-x-v1))
      (body))
    (rule mm-proof-capability-mandatory-x-v1
      (head
        (source-proof-relational-row2-v1
          mm-state-assertion-mandatory-variable
          capability-identity-v1 capability-x-v1))
      (body))

    (rule mm-proof-capability-label-active-a-v1
      (head
        (source-proof-relational-row2-v1
          mm-state-label-kind capability-ha-v1 mm-label-essential))
      (body))
    (rule mm-proof-capability-label-active-dummy-v1
      (head
        (source-proof-relational-row2-v1
          mm-state-label-kind capability-hdummy-v1 mm-label-essential))
      (body))
    (rule mm-proof-capability-label-x-v1
      (head
        (source-proof-relational-row2-v1
          mm-state-label-kind capability-hx-v1 mm-label-floating))
      (body))
    (rule mm-proof-capability-label-identity-v1
      (head
        (source-proof-relational-row2-v1
          mm-state-label-kind capability-identity-v1 mm-label-axiom))
      (body))

    (rule mm-proof-capability-order-active-a-v1
      (head
        (source-proof-relational-row3-v1
          mm-state-assertion-ordered-hypothesis capability-target-v1
          ProofTraceNatZeroV1 capability-ha-v1))
      (body))
    (rule mm-proof-capability-order-active-dummy-v1
      (head
        (source-proof-relational-row3-v1
          mm-state-assertion-ordered-hypothesis capability-target-v1
          (ProofTraceNatSuccV1 ProofTraceNatZeroV1)
          capability-hdummy-v1))
      (body))
    (rule mm-proof-capability-order-identity-x-v1
      (head
        (source-proof-relational-row3-v1
          mm-state-assertion-ordered-hypothesis capability-identity-v1
          ProofTraceNatZeroV1 capability-hx-v1))
      (body))
    (rule mm-proof-capability-frame-identity-x-v1
      (head
        (source-proof-relational-row3-v1
          mm-state-assertion-ordered-frame-hypothesis
          capability-identity-v1 ProofTraceNatZeroV1
          capability-hx-v1))
      (body))

    (rule mm-proof-capability-value-a-v1
      (head
        (source-proof-relational-value-list-v1
          MetamathProofV1 capability-formula-a-v1
          (ProofRelationalValueListConsV1 capability-wff-v1
            (ProofRelationalValueListConsV1 capability-a-v1
              ProofRelationalValueListNilV1))))
      (body))
    (rule mm-proof-capability-value-dummy-v1
      (head
        (source-proof-relational-value-list-v1
          MetamathProofV1 capability-formula-dummy-v1
          (ProofRelationalValueListConsV1 capability-wff-v1
            (ProofRelationalValueListConsV1 capability-dummy-v1
              ProofRelationalValueListNilV1))))
      (body))
    (rule mm-proof-capability-value-x-v1
      (head
        (source-proof-relational-value-list-v1
          MetamathProofV1 capability-formula-x-v1
          (ProofRelationalValueListConsV1 capability-wff-v1
            (ProofRelationalValueListConsV1 capability-x-v1
              ProofRelationalValueListNilV1))))
      (body))
    (rule mm-proof-capability-request-v1
      (head
        (source-proof-relational-request-v1
          MetamathProofV1 capability-request-v1 capability-target-v1
          capability-formula-a-v1))
      (body))

    (rule mm-proof-capability-context-count-v1
      (head
        (source-proof-trace-input-context-count-v1
          (ProofRelationalRequestV1 MetamathProofV1 capability-request-v1)
          (ProofTraceNatSuccV1
            (ProofTraceNatSuccV1 ProofTraceNatZeroV1))))
      (body))
    (rule mm-proof-capability-frame-count-v1
      (head
        (source-proof-trace-input-frame-count-v1
          (ProofRelationalLabelV1 MetamathProofV1 capability-identity-v1)
          (ProofTraceNatSuccV1 ProofTraceNatZeroV1)))
      (body))
    (rule mm-proof-capability-empty-apartness-v1
      (head
        (source-proof-trace-input-apartness-count-v1
          (ProofRelationalLabelV1 MetamathProofV1 capability-identity-v1)
          ProofTraceNatZeroV1))
      (body))
  ))
