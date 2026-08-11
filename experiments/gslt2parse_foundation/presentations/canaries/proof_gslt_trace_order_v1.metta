; A frame-order canary with one floating hypothesis, one essential
; hypothesis, and no apartness obligation.

(gslt-presentation-v1 ProofGSLTTraceOrderCanaryV1
  (signature)
  (equations)
  (rewrites
    (rule proof-trace-order-canary-variable-v1
      (head (ProofTokenVariableV1 trace-x))
      (body))
    (rule proof-trace-order-canary-literal-v1
      (head (ProofTokenLiteralV1 trace-turnstile))
      (body))
    (rule proof-trace-order-canary-assertion-v1
      (head
        (ProofAssertionDeclaredV1
          (ProofAssertionV1
            (ProofAssertionListConsV1
              (ProofAssertionVariableV1 trace-x trace-wff)
              ProofAssertionListNilV1)
            (ProofAssertionListConsV1
              (ProofAssertionEssentialV1
                (ProofSequenceConsV1 trace-turnstile
                  (ProofSequenceConsV1 trace-x ProofSequenceNilV1)))
              ProofAssertionListNilV1)
            trace-turnstile
            (ProofSequenceConsV1 trace-x ProofSequenceNilV1)
            ProofAssertionListNilV1)))
      (body))
  ))
