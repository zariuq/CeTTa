(gslt-language-v1
  (name provider-canary)
  (syntax-backend canary-reader-v1)
  (term-abi finite-horn-quote-v1)
  (semantic-source "semantics/provider_call_v1.metta")
  (program-carrier provider-program-nil provider-program-cons)
  (entry provider-entry 2 0 1)
  (observation bag))
