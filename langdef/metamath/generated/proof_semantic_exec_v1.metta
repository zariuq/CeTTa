; generated executable finite-Horn GSLT from a checked quotation
; quotation-answer-set-sha256 7216d70997c8129c5215cc53d5ed2d3ed9531e42456b99db1f889903b58c9dc2
(gslt-presentation-v1 GeneratedSemanticGSLTV1_7216d70997c8129c5215cc53d5ed2d3ed9531e42456b99db1f889903b58c9dc2
  (signature
    (operator MetamathProvableV1 1)
    (operator ProofAssertionBuildEnvironmentV1 3)
    (operator ProofAssertionCheckDisjointsV1 2)
    (operator ProofAssertionCheckEssentialsV1 3)
    (operator ProofAssertionDeclaredV1 1)
    (operator ProofAssertionDisjointV1 2)
    (operator ProofAssertionEssentialV1 1)
    (operator ProofAssertionListConsV1 2)
    (operator ProofAssertionListNilV1 0)
    (operator ProofAssertionV1 5)
    (operator ProofAssertionVariableV1 2)
    (operator ProofEnvironmentConsV1 3)
    (operator ProofEnvironmentLookupV1 3)
    (operator ProofEnvironmentNilV1 0)
    (operator ProofSequenceAppendV1 3)
    (operator ProofSequenceConsV1 2)
    (operator ProofSequenceInstantiateV1 3)
    (operator ProofSequenceNilV1 0)
    (operator ProofSequenceSupportApartV1 2)
    (operator ProofTokenAgainstSequenceV1 2)
    (operator ProofTokenApartV1 2)
    (operator ProofTokenDifferentV1 2)
    (operator ProofTokenLiteralV1 1)
    (operator ProofTokenPairAllowedV1 2)
    (operator ProofTokenVariableV1 1)
  )
  (equations)
  (rewrites
    (rule proof-assertion-apply-v1
      (head (MetamathProvableV1 (ProofSequenceConsV1 ?v2 ?v8)))
      (body
        (ProofAssertionDeclaredV1 (ProofAssertionV1 ?v0 ?v1 ?v2 ?v3 ?v4))
        (ProofAssertionBuildEnvironmentV1 ?v0 ?v5 ?v7)
        (ProofAssertionCheckEssentialsV1 ?v1 ?v6 ?v7)
        (ProofAssertionCheckDisjointsV1 ?v4 ?v7)
        (ProofSequenceInstantiateV1 ?v3 ?v7 ?v8)))
    (rule proof-assertion-build-environment-cons-v1
      (head (ProofAssertionBuildEnvironmentV1 (ProofAssertionListConsV1 (ProofAssertionVariableV1 ?v0 ?v1) ?v3) (ProofAssertionListConsV1 ?v2 ?v4) (ProofEnvironmentConsV1 ?v0 ?v2 ?v5)))
      (body
        (ProofTokenVariableV1 ?v0)
        (MetamathProvableV1 (ProofSequenceConsV1 ?v1 ?v2))
        (ProofAssertionBuildEnvironmentV1 ?v3 ?v4 ?v5)))
    (rule proof-assertion-build-environment-nil-v1
      (head (ProofAssertionBuildEnvironmentV1 (ProofAssertionListNilV1) (ProofAssertionListNilV1) (ProofEnvironmentNilV1)))
      (body))
    (rule proof-assertion-check-disjoints-cons-v1
      (head (ProofAssertionCheckDisjointsV1 (ProofAssertionListConsV1 (ProofAssertionDisjointV1 ?v0 ?v1) ?v2) ?v3))
      (body
        (ProofTokenDifferentV1 ?v0 ?v1)
        (ProofEnvironmentLookupV1 ?v3 ?v0 ?v4)
        (ProofEnvironmentLookupV1 ?v3 ?v1 ?v5)
        (ProofSequenceSupportApartV1 ?v4 ?v5)
        (ProofAssertionCheckDisjointsV1 ?v2 ?v3)))
    (rule proof-assertion-check-disjoints-nil-v1
      (head (ProofAssertionCheckDisjointsV1 (ProofAssertionListNilV1) ?v0))
      (body))
    (rule proof-assertion-check-essentials-cons-v1
      (head (ProofAssertionCheckEssentialsV1 (ProofAssertionListConsV1 (ProofAssertionEssentialV1 ?v0) ?v2) (ProofAssertionListConsV1 ?v1 ?v3) ?v4))
      (body
        (MetamathProvableV1 ?v1)
        (ProofSequenceInstantiateV1 ?v0 ?v4 ?v1)
        (ProofAssertionCheckEssentialsV1 ?v2 ?v3 ?v4)))
    (rule proof-assertion-check-essentials-nil-v1
      (head (ProofAssertionCheckEssentialsV1 (ProofAssertionListNilV1) (ProofAssertionListNilV1) ?v0))
      (body))
    (rule proof-assertion-use-premise-v1
      (head (MetamathProvableV1 ?v0))
      (body
        (MetamathProvableV1 ?v0)))
    (rule proof-environment-lookup-head-v1
      (head (ProofEnvironmentLookupV1 (ProofEnvironmentConsV1 ?v0 ?v1 ?v2) ?v0 ?v1))
      (body))
    (rule proof-environment-lookup-tail-v1
      (head (ProofEnvironmentLookupV1 (ProofEnvironmentConsV1 ?v0 ?v1 ?v2) ?v3 ?v4))
      (body
        (ProofTokenDifferentV1 ?v0 ?v3)
        (ProofEnvironmentLookupV1 ?v2 ?v3 ?v4)))
    (rule proof-sequence-append-cons-v1
      (head (ProofSequenceAppendV1 (ProofSequenceConsV1 ?v0 ?v1) ?v2 (ProofSequenceConsV1 ?v0 ?v3)))
      (body
        (ProofSequenceAppendV1 ?v1 ?v2 ?v3)))
    (rule proof-sequence-append-nil-v1
      (head (ProofSequenceAppendV1 (ProofSequenceNilV1) ?v0 ?v0))
      (body))
    (rule proof-sequence-instantiate-literal-v1
      (head (ProofSequenceInstantiateV1 (ProofSequenceConsV1 ?v0 ?v1) ?v2 (ProofSequenceConsV1 ?v0 ?v3)))
      (body
        (ProofTokenLiteralV1 ?v0)
        (ProofSequenceInstantiateV1 ?v1 ?v2 ?v3)))
    (rule proof-sequence-instantiate-nil-v1
      (head (ProofSequenceInstantiateV1 (ProofSequenceNilV1) ?v0 (ProofSequenceNilV1)))
      (body))
    (rule proof-sequence-instantiate-variable-v1
      (head (ProofSequenceInstantiateV1 (ProofSequenceConsV1 ?v0 ?v1) ?v2 ?v5))
      (body
        (ProofTokenVariableV1 ?v0)
        (ProofEnvironmentLookupV1 ?v2 ?v0 ?v3)
        (ProofSequenceInstantiateV1 ?v1 ?v2 ?v4)
        (ProofSequenceAppendV1 ?v3 ?v4 ?v5)))
    (rule proof-sequence-support-apart-cons-v1
      (head (ProofSequenceSupportApartV1 (ProofSequenceConsV1 ?v0 ?v1) ?v2))
      (body
        (ProofTokenAgainstSequenceV1 ?v0 ?v2)
        (ProofSequenceSupportApartV1 ?v1 ?v2)))
    (rule proof-sequence-support-apart-nil-v1
      (head (ProofSequenceSupportApartV1 (ProofSequenceNilV1) ?v0))
      (body))
    (rule proof-token-against-sequence-cons-v1
      (head (ProofTokenAgainstSequenceV1 ?v0 (ProofSequenceConsV1 ?v1 ?v2)))
      (body
        (ProofTokenPairAllowedV1 ?v0 ?v1)
        (ProofTokenAgainstSequenceV1 ?v0 ?v2)))
    (rule proof-token-against-sequence-nil-v1
      (head (ProofTokenAgainstSequenceV1 ?v0 (ProofSequenceNilV1)))
      (body))
    (rule proof-token-pair-apart-v1
      (head (ProofTokenPairAllowedV1 ?v0 ?v1))
      (body
        (ProofTokenVariableV1 ?v0)
        (ProofTokenVariableV1 ?v1)
        (ProofTokenApartV1 ?v0 ?v1)))
    (rule proof-token-pair-left-literal-v1
      (head (ProofTokenPairAllowedV1 ?v0 ?v1))
      (body
        (ProofTokenLiteralV1 ?v0)))
    (rule proof-token-pair-right-literal-v1
      (head (ProofTokenPairAllowedV1 ?v0 ?v1))
      (body
        (ProofTokenLiteralV1 ?v1)))
  ))
