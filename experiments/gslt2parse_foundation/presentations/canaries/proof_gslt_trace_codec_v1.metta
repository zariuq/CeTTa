; A vocabulary-independent fixture for raw-label and compressed-code lowering.
; The three labels deliberately denote only neutral trace instructions.

(gslt-presentation-v1 ProofGSLTTraceCodecCanaryV1
  (signature)
  (equations)
  (rewrites
    (rule proof-trace-codec-canary-label-m-v1
      (head
        (source-proof-trace-label-instruction-v1
          codec-label-m-v1 (ProofTracePushV1 codec-formula-m-v1)))
      (body))
    (rule proof-trace-codec-canary-label-e-v1
      (head
        (source-proof-trace-label-instruction-v1
          codec-label-e-v1 (ProofTracePushV1 codec-formula-e-v1)))
      (body))
    (rule proof-trace-codec-canary-label-r-v1
      (head
        (source-proof-trace-label-instruction-v1
          codec-label-r-v1 (ProofTracePushV1 codec-formula-r-v1)))
      (body))
    (rule proof-trace-codec-canary-label-r-different-m-v1
      (head
        (source-proof-trace-input-label-different-v1
          codec-label-r-v1 codec-label-m-v1))
      (body))
    (rule proof-trace-codec-canary-label-r-different-e-v1
      (head
        (source-proof-trace-input-label-different-v1
          codec-label-r-v1 codec-label-e-v1))
      (body))
  ))
