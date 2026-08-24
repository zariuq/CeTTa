; Metamath instantiates the generic sequence and assertion calculi without
; adding per-database rules.  Database symbols are ground runtime payloads;
; the fixed calculus checks their classification, substitution, hypotheses,
; and disjointness through explicit chronological evidence.

(gslt-presentation-v1 MetamathProofCalculusV1
  (signature)
  (equations)
  (rewrites
    (rule mm-proof-calculus-extension
      (head
        (source-proof-extension MetamathProofV1 MetamathLanguageV1))
      (body))
    (rule mm-proof-calculus-sequence-layer
      (head
        (source-proof-sequence-layer
          MetamathProofV1 MetamathLanguageV1))
      (body))
    (rule mm-proof-calculus-sequence-relational-layer
      (head
        (source-proof-sequence-relational-layer
          MetamathProofV1 MetamathLanguageV1))
      (body))
    (rule mm-proof-calculus-assertion-layer
      (head
        (source-proof-sequence-assertion-layer
          MetamathProofV1 MetamathLanguageV1 MetamathProvableV1))
      (body))
    (rule mm-proof-calculus-provable-judgment
      (head
        (source-proof-judgment
          MetamathProofV1 MetamathProvableV1
          (proof-succ-v1 proof-zero-v1)))
      (body))
  ))
