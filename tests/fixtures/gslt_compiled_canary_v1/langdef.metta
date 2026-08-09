(gslt-language-v1
  (name compiled-canary)
  (syntax-backend canary-reader-v1)
  (term-abi finite-horn-quote-v1)
  (semantic-source "semantics/renamed_answer_v1.metta")
  (program-carrier canary-program-nil canary-program-cons)
  (entry canary-evaluate 2 0 1)
  (observation bag))
