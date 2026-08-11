(gslt-language-v1
  (name metta-interact)
  (syntax-backend he-reader-direct-v1)
  (term-abi finite-horn-quote-v1)
  (semantic-source "../shared/finite_horn_quote_match_v1.metta")
  (semantic-source "semantics/interaction_core_v1.metta")
  (semantic-source "semantics/sequence_handler_v1.metta")
  (program-carrier interact-program-nil interact-program-cons)
  (request-pipeline
    interact-classify interact-produce interact-observe
    interact-produced-nil interact-produced-cons)
  (observation bag))
