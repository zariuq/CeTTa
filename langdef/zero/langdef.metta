(gslt-language-v1
  (name zero)
  (syntax-backend he-reader-direct-v1)
  (term-abi finite-horn-quote-v1)
  (semantic-source "../shared/finite_horn_quote_match_v1.metta")
  (semantic-source "semantics/query_kernel_v1.metta")
  (semantic-source "semantics/closed_bag_observation_v1.metta")
  (program-carrier zero-program-nil zero-program-cons)
  (request-pipeline
    zero-classify zero-produce zero-observe
    zero-produced-nil zero-produced-cons)
  (profile exp
    (semantic-source "semantics/semantic_runner_v1.metta"))
  (observation bag))
