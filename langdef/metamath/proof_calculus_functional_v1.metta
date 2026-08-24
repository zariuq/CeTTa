; Production Metamath composition of the generic sequence and assertion
; calculi.  It selects result-valued token classification and comparison while
; retaining the same proof judgments and authored assertion semantics.

(gslt-presentation-v1 MetamathProofFunctionalCalculusV1
  (signature)
  (equations)
  (rewrites
    (rule mm-proof-functional-calculus-extension
      (head
        (source-proof-extension MetamathProofV1 MetamathLanguageV1))
      (body))
    (rule mm-proof-functional-calculus-sequence-layer
      (head
        (source-proof-sequence-layer
          MetamathProofV1 MetamathLanguageV1))
      (body))
    (rule mm-proof-functional-calculus-sequence-functional-layer
      (head
        (source-proof-sequence-functional-layer
          MetamathProofV1 MetamathLanguageV1))
      (body))
    (rule mm-proof-functional-calculus-assertion-layer
      (head
        (source-proof-sequence-assertion-layer
          MetamathProofV1 MetamathLanguageV1 MetamathProvableV1))
      (body))
    (rule mm-proof-functional-calculus-provable-judgment
      (head
        (source-proof-judgment
          MetamathProofV1 MetamathProvableV1
          (proof-succ-v1 proof-zero-v1)))
      (body))
  ))
