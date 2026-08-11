; Binder-sensitive structural side conditions.  Source declarations refer to
; formals by name; the proof compiler resolves them to the executable ABI.

(gslt-presentation-v1 ProofGSLTBinderCanaryV1
  (signature)
  (equations)
  (rewrites
    (rule binder-proof-extension
      (head (source-proof-extension BinderProofV1 BinderLanguageV1))
      (body))
    (rule binder-base-k
      (head
        (source-proof-base-constructor
          BinderLanguageV1 K proof-zero-v1))
      (body))
    (rule binder-base-l
      (head
        (source-proof-base-constructor
          BinderLanguageV1 L proof-zero-v1))
      (body))
    (rule binder-judgment-relation
      (head
        (source-proof-judgment
          BinderProofV1 Rel
          (proof-succ-v1 (proof-succ-v1 proof-zero-v1))))
      (body))
    (rule binder-capability-binders
      (head
        (source-proof-capability
          BinderProofV1 proof-cap-binders-v1))
      (body))
    (rule binder-capability-explicit-substitution
      (head
        (source-proof-capability
          BinderProofV1 proof-cap-explicit-substitution-v1))
      (body))
    (rule binder-capability-substitution-condition
      (head
        (source-proof-capability
          BinderProofV1 proof-cap-substitution-condition-v1))
      (body))
    (rule binder-capability-unused-condition
      (head
        (source-proof-capability
          BinderProofV1 proof-cap-unused-binder-condition-v1))
      (body))
    (rule binder-capability-conversion
      (head
        (source-proof-capability
          BinderProofV1 proof-cap-conversion-declaration-v1))
      (body))
    (rule binder-conversion
      (head (source-proof-conversion BinderProofV1 Rel beta-v1))
      (body))
    (rule binder-rule-substitution
      (head
        (source-proof-rule
          BinderProofV1 subst-certificate
          (proof-list-cons-v1
            (proof-formal-v1 body (proof-succ-v1 proof-zero-v1))
            (proof-list-cons-v1
              (proof-formal-v1 replacement proof-zero-v1)
              (proof-list-cons-v1
                (proof-formal-v1 result proof-zero-v1)
                proof-list-nil-v1)))
          proof-list-nil-v1
          (proof-apply-v1 Rel
            (proof-list-cons-v1
              (proof-subst-v1
                (proof-fvar-v1 body)
                (proof-fvar-v1 replacement))
              (proof-list-cons-v1
                (proof-fvar-v1 result) proof-list-nil-v1)))
          (proof-list-cons-v1
            (proof-explicit-substitution-v1
              proof-zero-v1 body replacement result)
            proof-list-nil-v1)))
      (body))
    (rule binder-rule-unused
      (head
        (source-proof-rule
          BinderProofV1 drop-unused
          (proof-list-cons-v1
            (proof-formal-v1 body (proof-succ-v1 proof-zero-v1))
            (proof-list-cons-v1
              (proof-formal-v1 result proof-zero-v1)
              proof-list-nil-v1))
          proof-list-nil-v1
          (proof-apply-v1 Rel
            (proof-list-cons-v1
              (proof-lambda-v1 (proof-fvar-v1 body))
              (proof-list-cons-v1
                (proof-fvar-v1 result) proof-list-nil-v1)))
          (proof-list-cons-v1
            (proof-unused-binder-v1 proof-zero-v1 body result)
            proof-list-nil-v1)))
      (body))
    (rule binder-article-substitution
      (head
        (source-proof-article
          BinderProofV1 binder-substitution-canary
          proof-list-nil-v1
          (proof-list-cons-v1
            (proof-node-v1
              proof-zero-v1 subst-certificate
              (proof-list-cons-v1
                (proof-bvar-v1 proof-zero-v1)
                (proof-list-cons-v1
                  (proof-apply-v1 K proof-list-nil-v1)
                  (proof-list-cons-v1
                    (proof-apply-v1 K proof-list-nil-v1)
                    proof-list-nil-v1)))
              proof-list-nil-v1)
            proof-list-nil-v1)
          proof-zero-v1
          (proof-apply-v1 Rel
            (proof-list-cons-v1
              (proof-subst-v1
                (proof-bvar-v1 proof-zero-v1)
                (proof-apply-v1 K proof-list-nil-v1))
              (proof-list-cons-v1
                (proof-apply-v1 K proof-list-nil-v1)
                proof-list-nil-v1)))
          proof-require-rooted-v1))
      (body))
    (rule binder-article-unused
      (head
        (source-proof-article
          BinderProofV1 binder-unused-canary
          proof-list-nil-v1
          (proof-list-cons-v1
            (proof-node-v1
              proof-zero-v1 drop-unused
              (proof-list-cons-v1
                (proof-apply-v1 K proof-list-nil-v1)
                (proof-list-cons-v1
                  (proof-apply-v1 K proof-list-nil-v1)
                  proof-list-nil-v1))
              proof-list-nil-v1)
            proof-list-nil-v1)
          proof-zero-v1
          (proof-apply-v1 Rel
            (proof-list-cons-v1
              (proof-lambda-v1
                (proof-apply-v1 K proof-list-nil-v1))
              (proof-list-cons-v1
                (proof-apply-v1 K proof-list-nil-v1)
                proof-list-nil-v1)))
          proof-require-rooted-v1))
      (body))
  ))
