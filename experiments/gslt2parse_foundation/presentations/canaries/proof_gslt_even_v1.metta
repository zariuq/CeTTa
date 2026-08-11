; An independent natural-number calculus using the same proof compiler.

(gslt-presentation-v1 ProofGSLTEvenCanaryV1
  (signature)
  (equations)
  (rewrites
    (rule even-proof-extension
      (head (source-proof-extension EvenProofV1 NatLanguageV1))
      (body))
    (rule even-base-zero
      (head
        (source-proof-base-constructor
          NatLanguageV1 Z proof-zero-v1))
      (body))
    (rule even-base-successor
      (head
        (source-proof-base-constructor
          NatLanguageV1 S (proof-succ-v1 proof-zero-v1)))
      (body))
    (rule even-judgment
      (head
        (source-proof-judgment
          EvenProofV1 Even (proof-succ-v1 proof-zero-v1)))
      (body))
    (rule even-rule-zero
      (head
        (source-proof-rule
          EvenProofV1 zero
          proof-list-nil-v1
          proof-list-nil-v1
          (proof-apply-v1 Even
            (proof-list-cons-v1
              (proof-apply-v1 Z proof-list-nil-v1)
              proof-list-nil-v1))
          proof-list-nil-v1))
      (body))
    (rule even-rule-step-two
      (head
        (source-proof-rule
          EvenProofV1 step-two
          (proof-list-cons-v1
            (proof-formal-v1 N proof-zero-v1)
            proof-list-nil-v1)
          (proof-list-cons-v1
            (proof-apply-v1 Even
              (proof-list-cons-v1
                (proof-fvar-v1 N) proof-list-nil-v1))
            proof-list-nil-v1)
          (proof-apply-v1 Even
            (proof-list-cons-v1
              (proof-apply-v1 S
                (proof-list-cons-v1
                  (proof-apply-v1 S
                    (proof-list-cons-v1
                      (proof-fvar-v1 N) proof-list-nil-v1))
                  proof-list-nil-v1))
              proof-list-nil-v1))
          proof-list-nil-v1))
      (body))
    (rule even-article-two
      (head
        (source-proof-article
          EvenProofV1 even-two-canary
          proof-list-nil-v1
          (proof-list-cons-v1
            (proof-node-v1
              proof-zero-v1 zero proof-list-nil-v1 proof-list-nil-v1)
            (proof-list-cons-v1
              (proof-node-v1
                (proof-succ-v1 proof-zero-v1) step-two
                (proof-list-cons-v1
                  (proof-apply-v1 Z proof-list-nil-v1)
                  proof-list-nil-v1)
                (proof-list-cons-v1
                  (proof-node-ref-v1 proof-zero-v1)
                  proof-list-nil-v1))
              proof-list-nil-v1))
          (proof-succ-v1 proof-zero-v1)
          (proof-apply-v1 Even
            (proof-list-cons-v1
              (proof-apply-v1 S
                (proof-list-cons-v1
                  (proof-apply-v1 S
                    (proof-list-cons-v1
                      (proof-apply-v1 Z proof-list-nil-v1)
                      proof-list-nil-v1))
                  proof-list-nil-v1))
              proof-list-nil-v1))
          proof-require-rooted-v1))
      (body))
  ))
