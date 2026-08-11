(gslt-language-v1
  (name gslt-il)
  (syntax-backend he-reader-direct-v1)
  (term-abi finite-horn-quote-v1)
  (semantic-source "semantics/finite_indexed_command_v1.metta")
  (program-carrier gslt-il-program-nil gslt-il-program-cons)
  (request-pipeline
    gslt-il-classify gslt-il-produce gslt-il-observe
    gslt-il-produced-nil gslt-il-produced-cons)
  (observation bag))
