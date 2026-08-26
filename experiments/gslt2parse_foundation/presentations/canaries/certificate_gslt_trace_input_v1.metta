; A structurally small chronological-input guest.  Its full active context has
; two formulas, while its sole assertion stores one variable frame entry.  It
; declares no current-apartness or assertion-apartness rows.

(gslt-presentation-v1 CertificateGSLTTraceInputCanaryV1
  (signature)
  (equations)
  (rewrites
    (rule proof-trace-input-canary-variable-class-v1
      (head
        (source-proof-trace-input-token-class-v1
          input-x-v1 ProofTokenVariableClassV1))
      (body))
    (rule proof-trace-input-canary-token-equal-x-v1
      (head
        (source-proof-trace-input-token-compare-v1
          input-x-v1 input-x-v1 ProofTokenEqualV1))
      (body))
    (rule proof-trace-input-canary-token-equal-wff-v1
      (head
        (source-proof-trace-input-token-compare-v1
          input-wff-v1 input-wff-v1 ProofTokenEqualV1))
      (body))
    (rule proof-trace-input-canary-token-equal-a-v1
      (head
        (source-proof-trace-input-token-compare-v1
          input-a-v1 input-a-v1 ProofTokenEqualV1))
      (body))
    (rule proof-trace-input-canary-token-equal-dummy-v1
      (head
        (source-proof-trace-input-token-compare-v1
          input-dummy-v1 input-dummy-v1 ProofTokenEqualV1))
      (body))
    (rule proof-trace-input-canary-token-unequal-a-dummy-v1
      (head
        (source-proof-trace-input-token-compare-v1
          input-a-v1 input-dummy-v1 ProofTokenUnequalV1))
      (body))
    (rule proof-trace-input-canary-token-unequal-dummy-a-v1
      (head
        (source-proof-trace-input-token-compare-v1
          input-dummy-v1 input-a-v1 ProofTokenUnequalV1))
      (body))
    (rule proof-trace-input-canary-variable-v1
      (head (source-proof-trace-input-token-variable-v1 input-x-v1))
      (body))
    (rule proof-trace-input-canary-token-different-a-dummy-v1
      (head
        (source-proof-trace-input-token-different-v1
          input-a-v1 input-dummy-v1))
      (body))
    (rule proof-trace-input-canary-incomplete-normal-v1
      (head
        (source-proof-trace-input-incomplete-normal-label-v1
          input-unknown-v1))
      (body))
    (rule proof-trace-input-canary-request-v1
      (head
        (source-proof-trace-input-request-v1
          input-request-v1
          (ProofSequenceConsV1 input-wff-v1
            (ProofSequenceConsV1 input-a-v1 ProofSequenceNilV1))))
      (body))
    (rule proof-trace-input-canary-context-count-v1
      (head
        (source-proof-trace-input-context-count-v1
          input-request-v1
          (ProofTraceNatSuccV1
            (ProofTraceNatSuccV1 ProofTraceNatZeroV1))))
      (body))
    (rule proof-trace-input-canary-context-target-v1
      (head
        (source-proof-trace-input-context-v1
          input-request-v1 ProofTraceNatZeroV1
          (ProofSequenceConsV1 input-wff-v1
            (ProofSequenceConsV1 input-a-v1 ProofSequenceNilV1))))
      (body))
    (rule proof-trace-input-canary-context-dummy-v1
      (head
        (source-proof-trace-input-context-v1
          input-request-v1
          (ProofTraceNatSuccV1 ProofTraceNatZeroV1)
          (ProofSequenceConsV1 input-wff-v1
            (ProofSequenceConsV1 input-dummy-v1 ProofSequenceNilV1))))
      (body))

    (rule proof-trace-input-canary-push-target-v1
      (head
        (source-proof-trace-input-push-label-v1
          input-request-v1 input-hx-v1
          (ProofSequenceConsV1 input-wff-v1
            (ProofSequenceConsV1 input-a-v1 ProofSequenceNilV1))))
      (body))
    (rule proof-trace-input-canary-push-dummy-v1
      (head
        (source-proof-trace-input-push-label-v1
          input-request-v1 input-hdummy-v1
          (ProofSequenceConsV1 input-wff-v1
            (ProofSequenceConsV1 input-dummy-v1 ProofSequenceNilV1))))
      (body))
    (rule proof-trace-input-canary-identity-different-hx-v1
      (head
        (source-proof-trace-input-label-different-v1
          input-identity-v1 input-hx-v1))
      (body))

    (rule proof-trace-input-canary-assertion-v1
      (head
        (source-proof-trace-input-assertion-v1
          input-identity-v1 input-wff-v1
          (ProofSequenceConsV1 input-x-v1 ProofSequenceNilV1)))
      (body))
    (rule proof-trace-input-canary-frame-count-v1
      (head
        (source-proof-trace-input-frame-count-v1
          input-identity-v1
          (ProofTraceNatSuccV1 ProofTraceNatZeroV1)))
      (body))
    (rule proof-trace-input-canary-frame-variable-v1
      (head
        (source-proof-trace-input-frame-v1
          input-identity-v1 ProofTraceNatZeroV1
          (ProofTraceFrameVariableV1 input-x-v1 input-wff-v1)))
      (body))
    (rule proof-trace-input-canary-empty-apartness-v1
      (head
        (source-proof-trace-input-apartness-count-v1
          input-identity-v1 ProofTraceNatZeroV1))
      (body))

  ))
