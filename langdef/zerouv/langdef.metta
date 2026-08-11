(gslt-language-v1
  (name zerouv)
  (syntax-backend he-reader-direct-v1)
  (term-abi finite-horn-quote-v1)
  (semantic-source "../shared/finite_horn_quote_match_v1.metta")
  (semantic-source "../zero/semantics/query_kernel_v1.metta")
  (semantic-source "semantics/control_v1.metta")
  (program-carrier zero-program-nil zero-program-cons)
  (request-pipeline
    zerouv-classify zerouv-produce zerouv-observe
    zerouv-produced-nil zerouv-produced-cons)
  (observation bag))
