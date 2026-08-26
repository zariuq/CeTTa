; A frame-order canary with one floating hypothesis, one essential
; hypothesis, and no apartness obligation.

(gslt-presentation-v1 CertificateGSLTTraceOrderCanaryV1
  (signature)
  (equations)
  (rewrites
    (rule proof-trace-order-canary-variable-v1
      (head (ProofTokenVariableV1 trace-x))
      (body))
    (rule proof-trace-order-canary-literal-v1
      (head (ProofTokenLiteralV1 trace-turnstile))
      (body))
    (rule proof-trace-order-canary-compare-wff-v1
      (head
        (ProofTokenCompareV1
          trace-wff trace-wff ProofTokenEqualV1))
      (body))
    (rule proof-trace-order-canary-compare-a-v1
      (head
        (ProofTokenCompareV1
          trace-a trace-a ProofTokenEqualV1))
      (body))
    (rule proof-trace-order-canary-compare-x-v1
      (head
        (ProofTokenCompareV1
          trace-x trace-x ProofTokenEqualV1))
      (body))
    (rule proof-trace-order-canary-compare-turnstile-v1
      (head
        (ProofTokenCompareV1
          trace-turnstile trace-turnstile ProofTokenEqualV1))
      (body))
    (rule proof-trace-order-canary-compare-wff-turnstile-v1
      (head
        (ProofTokenCompareV1
          trace-wff trace-turnstile ProofTokenUnequalV1))
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
