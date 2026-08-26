; A propositional base language, a proof extension, and a closed article.

(gslt-presentation-v1 ProofGSLTPropCanaryV1
  (signature)
  (equations)
  (rewrites
    (rule prop-proof-extension
      (head (source-proof-extension PropProofV1 PropLanguageV1))
      (body))
    (rule prop-base-p
      (head
        (source-proof-base-constructor
          PropLanguageV1 p proof-zero-v1))
      (body))
    (rule prop-base-q
      (head
        (source-proof-base-constructor
          PropLanguageV1 q proof-zero-v1))
      (body))
    (rule prop-base-implication
      (head
        (source-proof-base-constructor
          PropLanguageV1 Imp
          (proof-succ-v1 (proof-succ-v1 proof-zero-v1))))
      (body))
    (rule prop-judgment-provable
      (head
        (source-proof-judgment
          PropProofV1 Prov (proof-succ-v1 proof-zero-v1)))
      (body))
    (rule prop-rule-given
      (head
        (source-proof-rule
          PropProofV1 given
          (proof-list-cons-v1
            (proof-formal-v1 X proof-zero-v1)
            proof-list-nil-v1)
          proof-list-nil-v1
          (proof-apply-v1 Prov
            (proof-list-cons-v1
              (proof-fvar-v1 X) proof-list-nil-v1))
          proof-list-nil-v1))
      (body))
    (rule prop-rule-modus-ponens
      (head
        (source-proof-rule
          PropProofV1 mp
          (proof-list-cons-v1
            (proof-formal-v1 X proof-zero-v1)
            (proof-list-cons-v1
              (proof-formal-v1 Y proof-zero-v1)
              proof-list-nil-v1))
          (proof-list-cons-v1
            (proof-apply-v1 Prov
              (proof-list-cons-v1
                (proof-apply-v1 Imp
                  (proof-list-cons-v1
                    (proof-fvar-v1 X)
                    (proof-list-cons-v1
                      (proof-fvar-v1 Y) proof-list-nil-v1)))
                proof-list-nil-v1))
            (proof-list-cons-v1
              (proof-apply-v1 Prov
                (proof-list-cons-v1
                  (proof-fvar-v1 X) proof-list-nil-v1))
              proof-list-nil-v1))
          (proof-apply-v1 Prov
            (proof-list-cons-v1
              (proof-fvar-v1 Y) proof-list-nil-v1))
          proof-list-nil-v1))
      (body))
    (rule prop-article-mp
      (head
        (source-proof-article
          PropProofV1 prop-mp-canary
          proof-list-nil-v1
          (proof-list-cons-v1
            (proof-node-v1
              proof-zero-v1 given
              (proof-list-cons-v1
                (proof-apply-v1 Imp
                  (proof-list-cons-v1
                    (proof-apply-v1 p proof-list-nil-v1)
                    (proof-list-cons-v1
                      (proof-apply-v1 q proof-list-nil-v1)
                      proof-list-nil-v1)))
                proof-list-nil-v1)
              proof-list-nil-v1)
            (proof-list-cons-v1
              (proof-node-v1
                (proof-succ-v1 proof-zero-v1) given
                (proof-list-cons-v1
                  (proof-apply-v1 p proof-list-nil-v1)
                  proof-list-nil-v1)
                proof-list-nil-v1)
              (proof-list-cons-v1
                (proof-node-v1
                  (proof-succ-v1
                    (proof-succ-v1 proof-zero-v1))
                  mp
                  (proof-list-cons-v1
                    (proof-apply-v1 p proof-list-nil-v1)
                    (proof-list-cons-v1
                      (proof-apply-v1 q proof-list-nil-v1)
                      proof-list-nil-v1))
                  (proof-list-cons-v1
                    (proof-node-ref-v1 proof-zero-v1)
                    (proof-list-cons-v1
                      (proof-node-ref-v1
                        (proof-succ-v1 proof-zero-v1))
                      proof-list-nil-v1)))
                proof-list-nil-v1)))
          (proof-succ-v1 (proof-succ-v1 proof-zero-v1))
          (proof-apply-v1 Prov
            (proof-list-cons-v1
              (proof-apply-v1 q proof-list-nil-v1)
              proof-list-nil-v1))
          proof-require-rooted-v1))
      (body))
  ))
