; A two-binder trace fixture that forces compressed decoding, saved-result
; restoration, ordered frame application, environment lookup, and a second
; assertion application to compose in one derivation.

(gslt-presentation-v1 ProofTraceCompressedCompositionCanaryV1
  (signature)
  (equations)
  (rewrites
    (rule compressed-canary-literal-wff-v1
      (head (source-proof-trace-input-token-literal-v1 compressed-wff-v1))
      (body))
    (rule compressed-canary-literal-left-v1
      (head (source-proof-trace-input-token-literal-v1 compressed-left-v1))
      (body))
    (rule compressed-canary-literal-arrow-v1
      (head (source-proof-trace-input-token-literal-v1 compressed-arrow-v1))
      (body))
    (rule compressed-canary-literal-right-v1
      (head (source-proof-trace-input-token-literal-v1 compressed-right-v1))
      (body))
    (rule compressed-canary-variable-left-v1
      (head (source-proof-trace-input-token-variable-v1 compressed-ph-v1))
      (body))
    (rule compressed-canary-variable-right-v1
      (head (source-proof-trace-input-token-variable-v1 compressed-ps-v1))
      (body))
    (rule compressed-canary-distinct-forward-v1
      (head
        (source-proof-trace-input-token-different-v1
          compressed-ph-v1 compressed-ps-v1))
      (body))
    (rule compressed-canary-distinct-reverse-v1
      (head
        (source-proof-trace-input-token-different-v1
          compressed-ps-v1 compressed-ph-v1))
      (body))

    (rule compressed-canary-request-v1
      (head
        (source-proof-trace-input-request-v1
          compressed-request-v1
          (ProofSequenceConsV1 compressed-wff-v1
            (ProofSequenceConsV1 compressed-left-v1
              (ProofSequenceConsV1 compressed-left-v1
                (ProofSequenceConsV1 compressed-ph-v1
                  (ProofSequenceConsV1 compressed-arrow-v1
                    (ProofSequenceConsV1 compressed-ps-v1
                      (ProofSequenceConsV1 compressed-right-v1
                        (ProofSequenceConsV1 compressed-arrow-v1
                          (ProofSequenceConsV1 compressed-left-v1
                            (ProofSequenceConsV1 compressed-ph-v1
                              (ProofSequenceConsV1 compressed-arrow-v1
                                (ProofSequenceConsV1 compressed-ps-v1
                                  (ProofSequenceConsV1 compressed-right-v1
                                    (ProofSequenceConsV1 compressed-right-v1
                                      ProofSequenceNilV1))))))))))))))))
      (body))
    (rule compressed-canary-context-count-v1
      (head
        (source-proof-trace-input-context-count-v1
          compressed-request-v1
          (ProofTraceNatSuccV1
            (ProofTraceNatSuccV1 ProofTraceNatZeroV1))))
      (body))
    (rule compressed-canary-context-left-v1
      (head
        (source-proof-trace-input-context-v1
          compressed-request-v1 ProofTraceNatZeroV1
          (ProofSequenceConsV1 compressed-wff-v1
            (ProofSequenceConsV1 compressed-ph-v1 ProofSequenceNilV1))))
      (body))
    (rule compressed-canary-context-right-v1
      (head
        (source-proof-trace-input-context-v1
          compressed-request-v1
          (ProofTraceNatSuccV1 ProofTraceNatZeroV1)
          (ProofSequenceConsV1 compressed-wff-v1
            (ProofSequenceConsV1 compressed-ps-v1 ProofSequenceNilV1))))
      (body))
    (rule compressed-canary-push-left-v1
      (head
        (source-proof-trace-input-push-label-v1
          compressed-request-v1 compressed-wph-v1
          (ProofSequenceConsV1 compressed-wff-v1
            (ProofSequenceConsV1 compressed-ph-v1 ProofSequenceNilV1))))
      (body))
    (rule compressed-canary-push-right-v1
      (head
        (source-proof-trace-input-push-label-v1
          compressed-request-v1 compressed-wps-v1
          (ProofSequenceConsV1 compressed-wff-v1
            (ProofSequenceConsV1 compressed-ps-v1 ProofSequenceNilV1))))
      (body))

    (rule compressed-canary-assertion-v1
      (head
        (source-proof-trace-input-assertion-v1
          compressed-wi-v1 compressed-wff-v1
          (ProofSequenceConsV1 compressed-left-v1
            (ProofSequenceConsV1 compressed-ph-v1
              (ProofSequenceConsV1 compressed-arrow-v1
                (ProofSequenceConsV1 compressed-ps-v1
                  (ProofSequenceConsV1 compressed-right-v1
                    ProofSequenceNilV1)))))))
      (body))
    (rule compressed-canary-frame-count-v1
      (head
        (source-proof-trace-input-frame-count-v1
          compressed-wi-v1
          (ProofTraceNatSuccV1
            (ProofTraceNatSuccV1 ProofTraceNatZeroV1))))
      (body))
    (rule compressed-canary-frame-left-v1
      (head
        (source-proof-trace-input-frame-v1
          compressed-wi-v1 ProofTraceNatZeroV1
          (ProofTraceFrameVariableV1 compressed-ph-v1 compressed-wff-v1)))
      (body))
    (rule compressed-canary-frame-right-v1
      (head
        (source-proof-trace-input-frame-v1
          compressed-wi-v1
          (ProofTraceNatSuccV1 ProofTraceNatZeroV1)
          (ProofTraceFrameVariableV1 compressed-ps-v1 compressed-wff-v1)))
      (body))
    (rule compressed-canary-apartness-count-v1
      (head
        (source-proof-trace-input-apartness-count-v1
          compressed-wi-v1 ProofTraceNatZeroV1))
      (body))
  ))
