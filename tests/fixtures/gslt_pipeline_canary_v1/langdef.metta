(gslt-language-v1
  (name pipeline-canary)
  (syntax-backend he-reader-direct-v1)
  (term-abi finite-horn-quote-v1)
  (semantic-source "semantics/renamed_pipeline_v1.metta")
  (program-carrier canary-program-nil canary-program-cons)
  (request-pipeline
    canary-classify canary-produce canary-observe
    canary-produced-nil canary-produced-cons)
  (observation bag))
