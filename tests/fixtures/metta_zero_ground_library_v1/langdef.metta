; Executable composition canary: the Zero core is unchanged, while an
; independently authored library supplies its open grounding relation.
(gslt-language-v1
  (name zero-ground-library-canary)
  (syntax-backend he-reader-direct-v1)
  (term-abi finite-horn-quote-v1)
  (semantic-source "../../../langdef/shared/finite_horn_quote_match_v1.metta")
  (semantic-source "../../../langdef/zero/semantics/query_kernel_v1.metta")
  (semantic-source "../../../langdef/zero/semantics/closed_bag_observation_v1.metta")
  (semantic-source "../metta_zero_ground_capability_v1.metta")
  (program-carrier zero-program-nil zero-program-cons)
  (request-pipeline
    zero-classify zero-produce zero-observe
    zero-produced-nil zero-produced-cons)
  (observation bag))
