; A closed sequence-instantiation article.  The assertion template contains
; one literal and two independently spliced variables.  Its support-apartness
; obligation is discharged by ordinary proof nodes, not a native condition.

(gslt-presentation-v1 ProofGSLTSequenceCanaryV1
  (signature)
  (equations)
  (rewrites
    (rule sequence-canary-extension
      (head
        (source-proof-extension
          SequenceProofV1 SequenceLanguageV1))
      (body))
    (rule sequence-canary-layer
      (head
        (source-proof-sequence-layer
          SequenceProofV1 SequenceLanguageV1))
      (body))
    (rule sequence-canary-assertion-layer
      (head
        (source-proof-sequence-assertion-layer
          SequenceProofV1 SequenceLanguageV1 SequenceProvableV1))
      (body))

    (rule sequence-canary-base-literal
      (head
        (source-proof-base-constructor
          SequenceLanguageV1 SequenceLiteralV1 proof-zero-v1))
      (body))
    (rule sequence-canary-base-x
      (head
        (source-proof-base-constructor
          SequenceLanguageV1 SequenceXv1 proof-zero-v1))
      (body))
    (rule sequence-canary-base-y
      (head
        (source-proof-base-constructor
          SequenceLanguageV1 SequenceYv1 proof-zero-v1))
      (body))
    (rule sequence-canary-base-a
      (head
        (source-proof-base-constructor
          SequenceLanguageV1 SequenceAv1 proof-zero-v1))
      (body))
    (rule sequence-canary-base-c
      (head
        (source-proof-base-constructor
          SequenceLanguageV1 SequenceCv1 proof-zero-v1))
      (body))
    (rule sequence-canary-base-typecode
      (head
        (source-proof-base-constructor
          SequenceLanguageV1 SequenceWffV1 proof-zero-v1))
      (body))
    (rule sequence-canary-judgment-provable
      (head
        (source-proof-judgment
          SequenceProofV1 SequenceProvableV1
          (proof-succ-v1 proof-zero-v1)))
      (body))

    (rule sequence-canary-fact-literal
      (head
        (source-proof-rule
          SequenceProofV1 sequence-canary-literal-v1
          proof-list-nil-v1
          proof-list-nil-v1
          (proof-apply-v1 ProofTokenLiteralV1
            (proof-list-cons-v1
              (proof-apply-v1 SequenceLiteralV1 proof-list-nil-v1)
              proof-list-nil-v1))
          proof-list-nil-v1))
      (body))
    (rule sequence-canary-fact-variable-x
      (head
        (source-proof-rule
          SequenceProofV1 sequence-canary-variable-x-v1
          proof-list-nil-v1
          proof-list-nil-v1
          (proof-apply-v1 ProofTokenVariableV1
            (proof-list-cons-v1
              (proof-apply-v1 SequenceXv1 proof-list-nil-v1)
              proof-list-nil-v1))
          proof-list-nil-v1))
      (body))
    (rule sequence-canary-fact-variable-y
      (head
        (source-proof-rule
          SequenceProofV1 sequence-canary-variable-y-v1
          proof-list-nil-v1
          proof-list-nil-v1
          (proof-apply-v1 ProofTokenVariableV1
            (proof-list-cons-v1
              (proof-apply-v1 SequenceYv1 proof-list-nil-v1)
              proof-list-nil-v1))
          proof-list-nil-v1))
      (body))
    (rule sequence-canary-fact-variable-a
      (head
        (source-proof-rule
          SequenceProofV1 sequence-canary-variable-a-v1
          proof-list-nil-v1
          proof-list-nil-v1
          (proof-apply-v1 ProofTokenVariableV1
            (proof-list-cons-v1
              (proof-apply-v1 SequenceAv1 proof-list-nil-v1)
              proof-list-nil-v1))
          proof-list-nil-v1))
      (body))
    (rule sequence-canary-fact-variable-c
      (head
        (source-proof-rule
          SequenceProofV1 sequence-canary-variable-c-v1
          proof-list-nil-v1
          proof-list-nil-v1
          (proof-apply-v1 ProofTokenVariableV1
            (proof-list-cons-v1
              (proof-apply-v1 SequenceCv1 proof-list-nil-v1)
              proof-list-nil-v1))
          proof-list-nil-v1))
      (body))
    (rule sequence-canary-fact-different-x-y
      (head
        (source-proof-rule
          SequenceProofV1 sequence-canary-different-x-y-v1
          proof-list-nil-v1
          proof-list-nil-v1
          (proof-apply-v1 ProofTokenDifferentV1
            (proof-list-cons-v1
              (proof-apply-v1 SequenceXv1 proof-list-nil-v1)
              (proof-list-cons-v1
                (proof-apply-v1 SequenceYv1 proof-list-nil-v1)
                proof-list-nil-v1)))
          proof-list-nil-v1))
      (body))
    (rule sequence-canary-fact-apart-a-c
      (head
        (source-proof-rule
          SequenceProofV1 sequence-canary-apart-a-c-v1
          proof-list-nil-v1
          proof-list-nil-v1
          (proof-apply-v1 ProofTokenApartV1
            (proof-list-cons-v1
              (proof-apply-v1 SequenceAv1 proof-list-nil-v1)
              (proof-list-cons-v1
                (proof-apply-v1 SequenceCv1 proof-list-nil-v1)
                proof-list-nil-v1)))
          proof-list-nil-v1))
      (body))

    (rule sequence-canary-given-x
      (head
        (source-proof-rule
          SequenceProofV1 sequence-canary-given-x-v1
          proof-list-nil-v1
          proof-list-nil-v1
          (proof-apply-v1 SequenceProvableV1
            (proof-list-cons-v1
              (proof-apply-v1 ProofSequenceConsV1
                (proof-list-cons-v1
                  (proof-apply-v1 SequenceWffV1 proof-list-nil-v1)
                  (proof-list-cons-v1
                    (proof-apply-v1 ProofSequenceConsV1
                      (proof-list-cons-v1
                        (proof-apply-v1 SequenceAv1 proof-list-nil-v1)
                        (proof-list-cons-v1
                          (proof-apply-v1
                            ProofSequenceNilV1 proof-list-nil-v1)
                          proof-list-nil-v1)))
                    proof-list-nil-v1)))
              proof-list-nil-v1))
          proof-list-nil-v1))
      (body))
    (rule sequence-canary-given-y
      (head
        (source-proof-rule
          SequenceProofV1 sequence-canary-given-y-v1
          proof-list-nil-v1
          proof-list-nil-v1
          (proof-apply-v1 SequenceProvableV1
            (proof-list-cons-v1
              (proof-apply-v1 ProofSequenceConsV1
                (proof-list-cons-v1
                  (proof-apply-v1 SequenceWffV1 proof-list-nil-v1)
                  (proof-list-cons-v1
                    (proof-apply-v1 ProofSequenceConsV1
                      (proof-list-cons-v1
                        (proof-apply-v1 SequenceCv1 proof-list-nil-v1)
                        (proof-list-cons-v1
                          (proof-apply-v1
                            ProofSequenceNilV1 proof-list-nil-v1)
                          proof-list-nil-v1)))
                    proof-list-nil-v1)))
              proof-list-nil-v1))
          proof-list-nil-v1))
      (body))
    (rule sequence-canary-given-essential
      (head
        (source-proof-rule
          SequenceProofV1 sequence-canary-given-essential-v1
          proof-list-nil-v1
          proof-list-nil-v1
          (proof-apply-v1 SequenceProvableV1
            (proof-list-cons-v1
              (proof-apply-v1 ProofSequenceConsV1
                (proof-list-cons-v1
                  (proof-apply-v1 SequenceAv1 proof-list-nil-v1)
                  (proof-list-cons-v1
                    (proof-apply-v1 ProofSequenceConsV1
                      (proof-list-cons-v1
                        (proof-apply-v1 SequenceCv1 proof-list-nil-v1)
                        (proof-list-cons-v1
                          (proof-apply-v1
                            ProofSequenceNilV1 proof-list-nil-v1)
                          proof-list-nil-v1)))
                    proof-list-nil-v1)))
              proof-list-nil-v1))
          proof-list-nil-v1))
      (body))

    ; Named source terms are document sugar.  The document presentation
    ; elaborates them to canonical proof terms before structural admission.
    (rule
          sequence-canary-term-literal
          (head
            (source-proof-article-term-v1
              SequenceProofV1
              sequence-splice-apart-canary
              literal
              (proof-apply-v1 SequenceLiteralV1 proof-list-nil-v1)))
          (body))
    (rule
          sequence-canary-term-x
          (head
            (source-proof-article-term-v1
              SequenceProofV1
              sequence-splice-apart-canary
              x
              (proof-apply-v1 SequenceXv1 proof-list-nil-v1)))
          (body))
    (rule
          sequence-canary-term-y
          (head
            (source-proof-article-term-v1
              SequenceProofV1
              sequence-splice-apart-canary
              y
              (proof-apply-v1 SequenceYv1 proof-list-nil-v1)))
          (body))
    (rule
          sequence-canary-term-a
          (head
            (source-proof-article-term-v1
              SequenceProofV1
              sequence-splice-apart-canary
              a
              (proof-apply-v1 SequenceAv1 proof-list-nil-v1)))
          (body))
    (rule
          sequence-canary-term-c
          (head
            (source-proof-article-term-v1
              SequenceProofV1
              sequence-splice-apart-canary
              c
              (proof-apply-v1 SequenceCv1 proof-list-nil-v1)))
          (body))
    (rule
          sequence-canary-term-wff
          (head
            (source-proof-article-term-v1
              SequenceProofV1
              sequence-splice-apart-canary
              wff
              (proof-apply-v1 SequenceWffV1 proof-list-nil-v1)))
          (body))
    (rule
          sequence-canary-term-sequence-nil
          (head
            (source-proof-article-term-v1
              SequenceProofV1
              sequence-splice-apart-canary
              sequence-nil
              (proof-apply-v1 ProofSequenceNilV1 proof-list-nil-v1)))
          (body))
    (rule
          sequence-canary-term-environment-nil
          (head
            (source-proof-article-term-v1
              SequenceProofV1
              sequence-splice-apart-canary
              environment-nil
              (proof-apply-v1 ProofEnvironmentNilV1 proof-list-nil-v1)))
          (body))
    (rule
          sequence-canary-term-sequence-y
          (head
            (source-proof-article-term-v1
              SequenceProofV1
              sequence-splice-apart-canary
              sequence-y
              (proof-apply-v1
                ProofSequenceConsV1
                (proof-list-cons-v1
                  (source-proof-term-ref-v1 y)
                  (proof-list-cons-v1 (source-proof-term-ref-v1 sequence-nil) proof-list-nil-v1)))))
          (body))
    (rule
          sequence-canary-term-sequence-a
          (head
            (source-proof-article-term-v1
              SequenceProofV1
              sequence-splice-apart-canary
              sequence-a
              (proof-apply-v1
                ProofSequenceConsV1
                (proof-list-cons-v1
                  (source-proof-term-ref-v1 a)
                  (proof-list-cons-v1 (source-proof-term-ref-v1 sequence-nil) proof-list-nil-v1)))))
          (body))
    (rule
          sequence-canary-term-sequence-c
          (head
            (source-proof-article-term-v1
              SequenceProofV1
              sequence-splice-apart-canary
              sequence-c
              (proof-apply-v1
                ProofSequenceConsV1
                (proof-list-cons-v1
                  (source-proof-term-ref-v1 c)
                  (proof-list-cons-v1 (source-proof-term-ref-v1 sequence-nil) proof-list-nil-v1)))))
          (body))
    (rule
          sequence-canary-term-sequence-x-y
          (head
            (source-proof-article-term-v1
              SequenceProofV1
              sequence-splice-apart-canary
              sequence-x-y
              (proof-apply-v1
                ProofSequenceConsV1
                (proof-list-cons-v1
                  (source-proof-term-ref-v1 x)
                  (proof-list-cons-v1
                    (proof-apply-v1
                      ProofSequenceConsV1
                      (proof-list-cons-v1
                        (source-proof-term-ref-v1 y)
                        (proof-list-cons-v1
                          (source-proof-term-ref-v1 sequence-nil)
                          proof-list-nil-v1)))
                    proof-list-nil-v1)))))
          (body))
    (rule
          sequence-canary-term-sequence-a-c
          (head
            (source-proof-article-term-v1
              SequenceProofV1
              sequence-splice-apart-canary
              sequence-a-c
              (proof-apply-v1
                ProofSequenceConsV1
                (proof-list-cons-v1
                  (source-proof-term-ref-v1 a)
                  (proof-list-cons-v1
                    (proof-apply-v1
                      ProofSequenceConsV1
                      (proof-list-cons-v1
                        (source-proof-term-ref-v1 c)
                        (proof-list-cons-v1
                          (source-proof-term-ref-v1 sequence-nil)
                          proof-list-nil-v1)))
                    proof-list-nil-v1)))))
          (body))
    (rule
          sequence-canary-term-result
          (head
            (source-proof-article-term-v1
              SequenceProofV1
              sequence-splice-apart-canary
              result
              (proof-apply-v1
                ProofSequenceConsV1
                (proof-list-cons-v1
                  (source-proof-term-ref-v1 literal)
                  (proof-list-cons-v1 (source-proof-term-ref-v1 sequence-a-c) proof-list-nil-v1)))))
          (body))
    (rule
          sequence-canary-term-environment-tail
          (head
            (source-proof-article-term-v1
              SequenceProofV1
              sequence-splice-apart-canary
              environment-tail
              (proof-apply-v1
                ProofEnvironmentConsV1
                (proof-list-cons-v1
                  (source-proof-term-ref-v1 y)
                  (proof-list-cons-v1
                    (source-proof-term-ref-v1 sequence-c)
                    (proof-list-cons-v1
                      (source-proof-term-ref-v1 environment-nil)
                      proof-list-nil-v1))))))
          (body))
    (rule
          sequence-canary-term-environment
          (head
            (source-proof-article-term-v1
              SequenceProofV1
              sequence-splice-apart-canary
              environment
              (proof-apply-v1
                ProofEnvironmentConsV1
                (proof-list-cons-v1
                  (source-proof-term-ref-v1 x)
                  (proof-list-cons-v1
                    (source-proof-term-ref-v1 sequence-a)
                    (proof-list-cons-v1
                      (source-proof-term-ref-v1 environment-tail)
                      proof-list-nil-v1))))))
          (body))
    (rule
          sequence-canary-term-assertion-list-nil
          (head
            (source-proof-article-term-v1
              SequenceProofV1
              sequence-splice-apart-canary
              assertion-list-nil
              (proof-apply-v1
                ProofAssertionListNilV1 proof-list-nil-v1)))
          (body))
    (rule
          sequence-canary-term-assertion-variables-tail
          (head
            (source-proof-article-term-v1
              SequenceProofV1
              sequence-splice-apart-canary
              assertion-variables-tail
              (proof-apply-v1 ProofAssertionListConsV1
                (proof-list-cons-v1
                  (proof-apply-v1 ProofAssertionVariableV1
                    (proof-list-cons-v1
                      (source-proof-term-ref-v1 y)
                      (proof-list-cons-v1
                        (source-proof-term-ref-v1 wff)
                        proof-list-nil-v1)))
                  (proof-list-cons-v1
                    (source-proof-term-ref-v1 assertion-list-nil)
                    proof-list-nil-v1)))))
          (body))
    (rule
          sequence-canary-term-assertion-variables
          (head
            (source-proof-article-term-v1
              SequenceProofV1
              sequence-splice-apart-canary
              assertion-variables
              (proof-apply-v1 ProofAssertionListConsV1
                (proof-list-cons-v1
                  (proof-apply-v1 ProofAssertionVariableV1
                    (proof-list-cons-v1
                      (source-proof-term-ref-v1 x)
                      (proof-list-cons-v1
                        (source-proof-term-ref-v1 wff)
                        proof-list-nil-v1)))
                  (proof-list-cons-v1
                    (source-proof-term-ref-v1 assertion-variables-tail)
                    proof-list-nil-v1)))))
          (body))
    (rule
          sequence-canary-term-assertion-essentials
          (head
            (source-proof-article-term-v1
              SequenceProofV1
              sequence-splice-apart-canary
              assertion-essentials
              (proof-apply-v1 ProofAssertionListConsV1
                (proof-list-cons-v1
                  (proof-apply-v1 ProofAssertionEssentialV1
                    (proof-list-cons-v1
                      (source-proof-term-ref-v1 sequence-x-y)
                      proof-list-nil-v1))
                  (proof-list-cons-v1
                    (source-proof-term-ref-v1 assertion-list-nil)
                    proof-list-nil-v1)))))
          (body))
    (rule
          sequence-canary-term-assertion-disjoints
          (head
            (source-proof-article-term-v1
              SequenceProofV1
              sequence-splice-apart-canary
              assertion-disjoints
              (proof-apply-v1 ProofAssertionListConsV1
                (proof-list-cons-v1
                  (proof-apply-v1 ProofAssertionDisjointV1
                    (proof-list-cons-v1
                      (source-proof-term-ref-v1 x)
                      (proof-list-cons-v1
                        (source-proof-term-ref-v1 y)
                        proof-list-nil-v1)))
                  (proof-list-cons-v1
                    (source-proof-term-ref-v1 assertion-list-nil)
                    proof-list-nil-v1)))))
          (body))
    (rule
          sequence-canary-term-conclusion-template
          (head
            (source-proof-article-term-v1
              SequenceProofV1
              sequence-splice-apart-canary
              conclusion-template
              (proof-apply-v1 ProofSequenceConsV1
                (proof-list-cons-v1
                  (source-proof-term-ref-v1 literal)
                  (proof-list-cons-v1
                    (source-proof-term-ref-v1 sequence-x-y)
                    proof-list-nil-v1)))))
          (body))
    (rule
          sequence-canary-term-assertion
          (head
            (source-proof-article-term-v1
              SequenceProofV1
              sequence-splice-apart-canary
              assertion
              (proof-apply-v1 ProofAssertionV1
                (proof-list-cons-v1
                  (source-proof-term-ref-v1 assertion-variables)
                  (proof-list-cons-v1
                    (source-proof-term-ref-v1 assertion-essentials)
                    (proof-list-cons-v1
                      (source-proof-term-ref-v1 wff)
                      (proof-list-cons-v1
                        (source-proof-term-ref-v1 conclusion-template)
                        (proof-list-cons-v1
                          (source-proof-term-ref-v1 assertion-disjoints)
                          proof-list-nil-v1))))))))
          (body))
    (rule
          sequence-canary-term-assertion-declared
          (head
            (source-proof-article-term-v1
              SequenceProofV1
              sequence-splice-apart-canary
              assertion-declared
              (proof-apply-v1 ProofAssertionDeclaredV1
                (proof-list-cons-v1
                  (source-proof-term-ref-v1 assertion)
                  proof-list-nil-v1))))
          (body))
    (rule
          sequence-canary-term-assertion-images-tail
          (head
            (source-proof-article-term-v1
              SequenceProofV1
              sequence-splice-apart-canary
              assertion-images-tail
              (proof-apply-v1 ProofAssertionListConsV1
                (proof-list-cons-v1
                  (source-proof-term-ref-v1 sequence-c)
                  (proof-list-cons-v1
                    (source-proof-term-ref-v1 assertion-list-nil)
                    proof-list-nil-v1)))))
          (body))
    (rule
          sequence-canary-term-assertion-images
          (head
            (source-proof-article-term-v1
              SequenceProofV1
              sequence-splice-apart-canary
              assertion-images
              (proof-apply-v1 ProofAssertionListConsV1
                (proof-list-cons-v1
                  (source-proof-term-ref-v1 sequence-a)
                  (proof-list-cons-v1
                    (source-proof-term-ref-v1 assertion-images-tail)
                    proof-list-nil-v1)))))
          (body))
    (rule
          sequence-canary-term-assertion-actuals
          (head
            (source-proof-article-term-v1
              SequenceProofV1
              sequence-splice-apart-canary
              assertion-actuals
              (proof-apply-v1 ProofAssertionListConsV1
                (proof-list-cons-v1
                  (source-proof-term-ref-v1 sequence-a-c)
                  (proof-list-cons-v1
                    (source-proof-term-ref-v1 assertion-list-nil)
                    proof-list-nil-v1)))))
          (body))
    (rule
          sequence-canary-term-goal
          (head
            (source-proof-article-term-v1
              SequenceProofV1
              sequence-splice-apart-canary
              goal
              (proof-apply-v1
                SequenceProvableV1
                (proof-list-cons-v1
                  (proof-apply-v1
                    ProofSequenceConsV1
                    (proof-list-cons-v1
                      (source-proof-term-ref-v1 wff)
                      (proof-list-cons-v1 (source-proof-term-ref-v1 result) proof-list-nil-v1)))
                  proof-list-nil-v1))))
          (body))

    ; Each document node names one rule application and symbolic earlier-node
    ; references.  The generic document layer assigns canonical Peano indices.
    (rule
          sequence-canary-node-literal
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-literal
              sequence-canary-literal-v1
              proof-list-nil-v1
              proof-list-nil-v1))
          (body))
    (rule
          sequence-canary-node-variable-x
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-variable-x
              sequence-canary-variable-x-v1
              proof-list-nil-v1
              proof-list-nil-v1))
          (body))
    (rule
          sequence-canary-node-variable-y
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-variable-y
              sequence-canary-variable-y-v1
              proof-list-nil-v1
              proof-list-nil-v1))
          (body))
    (rule
          sequence-canary-node-variable-a
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-variable-a
              sequence-canary-variable-a-v1
              proof-list-nil-v1
              proof-list-nil-v1))
          (body))
    (rule
          sequence-canary-node-variable-c
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-variable-c
              sequence-canary-variable-c-v1
              proof-list-nil-v1
              proof-list-nil-v1))
          (body))
    (rule
          sequence-canary-node-different-x-y
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-different-x-y
              sequence-canary-different-x-y-v1
              proof-list-nil-v1
              proof-list-nil-v1))
          (body))
    (rule
          sequence-canary-node-apart-a-c
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-apart-a-c
              sequence-canary-apart-a-c-v1
              proof-list-nil-v1
              proof-list-nil-v1))
          (body))
    (rule
          sequence-canary-node-given-x
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-given-x
              sequence-canary-given-x-v1
              proof-list-nil-v1
              proof-list-nil-v1))
          (body))
    (rule
          sequence-canary-node-given-y
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-given-y
              sequence-canary-given-y-v1
              proof-list-nil-v1
              proof-list-nil-v1))
          (body))
    (rule
          sequence-canary-node-given-essential
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-given-essential
              sequence-canary-given-essential-v1
              proof-list-nil-v1
              proof-list-nil-v1))
          (body))
    (rule
          sequence-canary-node-lookup-y-tail
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-lookup-y-tail
              proof-environment-lookup-head-v1
              (proof-list-cons-v1
                (source-proof-term-ref-v1 y)
                (proof-list-cons-v1
                  (source-proof-term-ref-v1 sequence-c)
                  (proof-list-cons-v1
                    (source-proof-term-ref-v1 environment-nil)
                    proof-list-nil-v1)))
              proof-list-nil-v1))
          (body))
    (rule
          sequence-canary-node-lookup-y
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-lookup-y
              proof-environment-lookup-tail-v1
              (proof-list-cons-v1
                (source-proof-term-ref-v1 x)
                (proof-list-cons-v1
                  (source-proof-term-ref-v1 sequence-a)
                  (proof-list-cons-v1
                    (source-proof-term-ref-v1 environment-tail)
                    (proof-list-cons-v1
                      (source-proof-term-ref-v1 y)
                      (proof-list-cons-v1
                        (source-proof-term-ref-v1 sequence-c)
                        proof-list-nil-v1)))))
              (proof-list-cons-v1
                (source-proof-node-ref-v1 node-different-x-y)
                (proof-list-cons-v1
                  (source-proof-node-ref-v1 node-lookup-y-tail)
                  proof-list-nil-v1))))
          (body))
    (rule
          sequence-canary-node-lookup-x
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-lookup-x
              proof-environment-lookup-head-v1
              (proof-list-cons-v1
                (source-proof-term-ref-v1 x)
                (proof-list-cons-v1
                  (source-proof-term-ref-v1 sequence-a)
                  (proof-list-cons-v1
                    (source-proof-term-ref-v1 environment-tail)
                    proof-list-nil-v1)))
              proof-list-nil-v1))
          (body))
    (rule
          sequence-canary-node-instantiate-nil
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-instantiate-nil
              proof-sequence-instantiate-nil-v1
              (proof-list-cons-v1 (source-proof-term-ref-v1 environment) proof-list-nil-v1)
              proof-list-nil-v1))
          (body))
    (rule
          sequence-canary-node-append-empty
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-append-empty
              proof-sequence-append-nil-v1
              (proof-list-cons-v1 (source-proof-term-ref-v1 sequence-nil) proof-list-nil-v1)
              proof-list-nil-v1))
          (body))
    (rule
          sequence-canary-node-append-c
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-append-c
              proof-sequence-append-cons-v1
              (proof-list-cons-v1
                (source-proof-term-ref-v1 c)
                (proof-list-cons-v1
                  (source-proof-term-ref-v1 sequence-nil)
                  (proof-list-cons-v1
                    (source-proof-term-ref-v1 sequence-nil)
                    (proof-list-cons-v1
                      (source-proof-term-ref-v1 sequence-nil)
                      proof-list-nil-v1))))
              (proof-list-cons-v1
                (source-proof-node-ref-v1 node-append-empty)
                proof-list-nil-v1)))
          (body))
    (rule
          sequence-canary-node-instantiate-y
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-instantiate-y
              proof-sequence-instantiate-variable-v1
              (proof-list-cons-v1
                (source-proof-term-ref-v1 y)
                (proof-list-cons-v1
                  (source-proof-term-ref-v1 sequence-nil)
                  (proof-list-cons-v1
                    (source-proof-term-ref-v1 environment)
                    (proof-list-cons-v1
                      (source-proof-term-ref-v1 sequence-c)
                      (proof-list-cons-v1
                        (source-proof-term-ref-v1 sequence-nil)
                        (proof-list-cons-v1
                          (source-proof-term-ref-v1 sequence-c)
                          proof-list-nil-v1))))))
              (proof-list-cons-v1
                (source-proof-node-ref-v1 node-variable-y)
                (proof-list-cons-v1
                  (source-proof-node-ref-v1 node-lookup-y)
                  (proof-list-cons-v1
                    (source-proof-node-ref-v1 node-instantiate-nil)
                    (proof-list-cons-v1
                      (source-proof-node-ref-v1 node-append-c)
                      proof-list-nil-v1))))))
          (body))
    (rule
          sequence-canary-node-append-to-c
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-append-to-c
              proof-sequence-append-nil-v1
              (proof-list-cons-v1 (source-proof-term-ref-v1 sequence-c) proof-list-nil-v1)
              proof-list-nil-v1))
          (body))
    (rule
          sequence-canary-node-append-a-c
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-append-a-c
              proof-sequence-append-cons-v1
              (proof-list-cons-v1
                (source-proof-term-ref-v1 a)
                (proof-list-cons-v1
                  (source-proof-term-ref-v1 sequence-nil)
                  (proof-list-cons-v1
                    (source-proof-term-ref-v1 sequence-c)
                    (proof-list-cons-v1 (source-proof-term-ref-v1 sequence-c) proof-list-nil-v1))))
              (proof-list-cons-v1 (source-proof-node-ref-v1 node-append-to-c) proof-list-nil-v1)))
          (body))
    (rule
          sequence-canary-node-instantiate-x-y
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-instantiate-x-y
              proof-sequence-instantiate-variable-v1
              (proof-list-cons-v1
                (source-proof-term-ref-v1 x)
                (proof-list-cons-v1
                  (source-proof-term-ref-v1 sequence-y)
                  (proof-list-cons-v1
                    (source-proof-term-ref-v1 environment)
                    (proof-list-cons-v1
                      (source-proof-term-ref-v1 sequence-a)
                      (proof-list-cons-v1
                        (source-proof-term-ref-v1 sequence-c)
                        (proof-list-cons-v1
                          (source-proof-term-ref-v1 sequence-a-c)
                          proof-list-nil-v1))))))
              (proof-list-cons-v1
                (source-proof-node-ref-v1 node-variable-x)
                (proof-list-cons-v1
                  (source-proof-node-ref-v1 node-lookup-x)
                  (proof-list-cons-v1
                    (source-proof-node-ref-v1 node-instantiate-y)
                    (proof-list-cons-v1
                      (source-proof-node-ref-v1 node-append-a-c)
                      proof-list-nil-v1))))))
          (body))
    (rule
          sequence-canary-node-instantiate-literal
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-instantiate-literal
              proof-sequence-instantiate-literal-v1
              (proof-list-cons-v1
                (source-proof-term-ref-v1 literal)
                (proof-list-cons-v1
                  (source-proof-term-ref-v1 sequence-x-y)
                  (proof-list-cons-v1
                    (source-proof-term-ref-v1 environment)
                    (proof-list-cons-v1
                      (source-proof-term-ref-v1 sequence-a-c)
                      proof-list-nil-v1))))
              (proof-list-cons-v1
                (source-proof-node-ref-v1 node-literal)
                (proof-list-cons-v1
                  (source-proof-node-ref-v1 node-instantiate-x-y)
                  proof-list-nil-v1))))
          (body))
    (rule
          sequence-canary-node-pair-a-c
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-pair-a-c
              proof-token-pair-apart-v1
              (proof-list-cons-v1
                (source-proof-term-ref-v1 a)
                (proof-list-cons-v1 (source-proof-term-ref-v1 c) proof-list-nil-v1))
              (proof-list-cons-v1
                (source-proof-node-ref-v1 node-variable-a)
                (proof-list-cons-v1
                  (source-proof-node-ref-v1 node-variable-c)
                  (proof-list-cons-v1
                    (source-proof-node-ref-v1 node-apart-a-c)
                    proof-list-nil-v1)))))
          (body))
    (rule
          sequence-canary-node-against-nil
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-against-nil
              proof-token-against-sequence-nil-v1
              (proof-list-cons-v1 (source-proof-term-ref-v1 a) proof-list-nil-v1)
              proof-list-nil-v1))
          (body))
    (rule
          sequence-canary-node-against-c
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-against-c
              proof-token-against-sequence-cons-v1
              (proof-list-cons-v1
                (source-proof-term-ref-v1 a)
                (proof-list-cons-v1
                  (source-proof-term-ref-v1 c)
                  (proof-list-cons-v1 (source-proof-term-ref-v1 sequence-nil) proof-list-nil-v1)))
              (proof-list-cons-v1
                (source-proof-node-ref-v1 node-pair-a-c)
                (proof-list-cons-v1
                  (source-proof-node-ref-v1 node-against-nil)
                  proof-list-nil-v1))))
          (body))
    (rule
          sequence-canary-node-support-nil
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-support-nil
              proof-sequence-support-apart-nil-v1
              (proof-list-cons-v1 (source-proof-term-ref-v1 sequence-c) proof-list-nil-v1)
              proof-list-nil-v1))
          (body))
    (rule
          sequence-canary-node-support-a-c
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-support-a-c
              proof-sequence-support-apart-cons-v1
              (proof-list-cons-v1
                (source-proof-term-ref-v1 a)
                (proof-list-cons-v1
                  (source-proof-term-ref-v1 sequence-nil)
                  (proof-list-cons-v1 (source-proof-term-ref-v1 sequence-c) proof-list-nil-v1)))
              (proof-list-cons-v1
                (source-proof-node-ref-v1 node-against-c)
                (proof-list-cons-v1
                  (source-proof-node-ref-v1 node-support-nil)
                  proof-list-nil-v1))))
          (body))
    (rule
          sequence-canary-node-build-environment-nil
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-build-environment-nil
              proof-assertion-build-environment-nil-v1
              proof-list-nil-v1
              proof-list-nil-v1))
          (body))
    (rule
          sequence-canary-node-build-environment-y
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-build-environment-y
              proof-assertion-build-environment-cons-v1
              (proof-list-cons-v1
                (source-proof-term-ref-v1 y)
                (proof-list-cons-v1
                  (source-proof-term-ref-v1 wff)
                  (proof-list-cons-v1
                    (source-proof-term-ref-v1 sequence-c)
                    (proof-list-cons-v1
                      (source-proof-term-ref-v1 assertion-list-nil)
                      (proof-list-cons-v1
                        (source-proof-term-ref-v1 assertion-list-nil)
                        (proof-list-cons-v1
                          (source-proof-term-ref-v1 environment-nil)
                          proof-list-nil-v1))))))
              (proof-list-cons-v1
                (source-proof-node-ref-v1 node-variable-y)
                (proof-list-cons-v1
                  (source-proof-node-ref-v1 node-given-y)
                  (proof-list-cons-v1
                    (source-proof-node-ref-v1 node-build-environment-nil)
                    proof-list-nil-v1)))))
          (body))
    (rule
          sequence-canary-node-build-environment-x
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-build-environment-x
              proof-assertion-build-environment-cons-v1
              (proof-list-cons-v1
                (source-proof-term-ref-v1 x)
                (proof-list-cons-v1
                  (source-proof-term-ref-v1 wff)
                  (proof-list-cons-v1
                    (source-proof-term-ref-v1 sequence-a)
                    (proof-list-cons-v1
                      (source-proof-term-ref-v1 assertion-variables-tail)
                      (proof-list-cons-v1
                        (source-proof-term-ref-v1 assertion-images-tail)
                        (proof-list-cons-v1
                          (source-proof-term-ref-v1 environment-tail)
                          proof-list-nil-v1))))))
              (proof-list-cons-v1
                (source-proof-node-ref-v1 node-variable-x)
                (proof-list-cons-v1
                  (source-proof-node-ref-v1 node-given-x)
                  (proof-list-cons-v1
                    (source-proof-node-ref-v1 node-build-environment-y)
                    proof-list-nil-v1)))))
          (body))
    (rule
          sequence-canary-node-check-essentials-nil
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-check-essentials-nil
              proof-assertion-check-essentials-nil-v1
              (proof-list-cons-v1
                (source-proof-term-ref-v1 environment)
                proof-list-nil-v1)
              proof-list-nil-v1))
          (body))
    (rule
          sequence-canary-node-check-essentials
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-check-essentials
              proof-assertion-check-essentials-cons-v1
              (proof-list-cons-v1
                (source-proof-term-ref-v1 sequence-x-y)
                (proof-list-cons-v1
                  (source-proof-term-ref-v1 sequence-a-c)
                  (proof-list-cons-v1
                    (source-proof-term-ref-v1 assertion-list-nil)
                    (proof-list-cons-v1
                      (source-proof-term-ref-v1 assertion-list-nil)
                      (proof-list-cons-v1
                        (source-proof-term-ref-v1 environment)
                        proof-list-nil-v1)))))
              (proof-list-cons-v1
                (source-proof-node-ref-v1 node-given-essential)
                (proof-list-cons-v1
                  (source-proof-node-ref-v1 node-instantiate-x-y)
                  (proof-list-cons-v1
                    (source-proof-node-ref-v1 node-check-essentials-nil)
                    proof-list-nil-v1)))))
          (body))
    (rule
          sequence-canary-node-check-disjoints-nil
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-check-disjoints-nil
              proof-assertion-check-disjoints-nil-v1
              (proof-list-cons-v1
                (source-proof-term-ref-v1 environment)
                proof-list-nil-v1)
              proof-list-nil-v1))
          (body))
    (rule
          sequence-canary-node-check-disjoints
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-check-disjoints
              proof-assertion-check-disjoints-cons-v1
              (proof-list-cons-v1
                (source-proof-term-ref-v1 x)
                (proof-list-cons-v1
                  (source-proof-term-ref-v1 y)
                  (proof-list-cons-v1
                    (source-proof-term-ref-v1 assertion-list-nil)
                    (proof-list-cons-v1
                      (source-proof-term-ref-v1 environment)
                      (proof-list-cons-v1
                        (source-proof-term-ref-v1 sequence-a)
                        (proof-list-cons-v1
                          (source-proof-term-ref-v1 sequence-c)
                          proof-list-nil-v1))))))
              (proof-list-cons-v1
                (source-proof-node-ref-v1 node-different-x-y)
                (proof-list-cons-v1
                  (source-proof-node-ref-v1 node-lookup-x)
                  (proof-list-cons-v1
                    (source-proof-node-ref-v1 node-lookup-y)
                    (proof-list-cons-v1
                      (source-proof-node-ref-v1 node-support-a-c)
                      (proof-list-cons-v1
                        (source-proof-node-ref-v1 node-check-disjoints-nil)
                        proof-list-nil-v1)))))))
          (body))
    (rule
          sequence-canary-node-apply
          (head
            (source-proof-article-node-v1
              SequenceProofV1
              sequence-splice-apart-canary
              node-apply
              proof-assertion-apply-v1
              (proof-list-cons-v1
                (source-proof-term-ref-v1 assertion-variables)
                (proof-list-cons-v1
                  (source-proof-term-ref-v1 assertion-essentials)
                  (proof-list-cons-v1
                    (source-proof-term-ref-v1 wff)
                    (proof-list-cons-v1
                      (source-proof-term-ref-v1 conclusion-template)
                      (proof-list-cons-v1
                        (source-proof-term-ref-v1 assertion-disjoints)
                        (proof-list-cons-v1
                          (source-proof-term-ref-v1 assertion-images)
                          (proof-list-cons-v1
                            (source-proof-term-ref-v1 assertion-actuals)
                            (proof-list-cons-v1
                              (source-proof-term-ref-v1 environment)
                              (proof-list-cons-v1
                                (source-proof-term-ref-v1 result)
                                proof-list-nil-v1)))))))))
              (proof-list-cons-v1
                (proof-premise-ref-v1 proof-zero-v1)
                (proof-list-cons-v1
                  (source-proof-node-ref-v1 node-build-environment-x)
                  (proof-list-cons-v1
                    (source-proof-node-ref-v1 node-check-essentials)
                    (proof-list-cons-v1
                      (source-proof-node-ref-v1 node-check-disjoints)
                      (proof-list-cons-v1
                        (source-proof-node-ref-v1 node-instantiate-literal)
                        proof-list-nil-v1)))))))
          (body))

    (rule
          sequence-canary-order
          (head
            (source-proof-article-order-v1
              SequenceProofV1
              sequence-splice-apart-canary
              (proof-list-cons-v1
                node-literal
                (proof-list-cons-v1
                  node-variable-x
                  (proof-list-cons-v1
                    node-variable-y
                    (proof-list-cons-v1
                      node-variable-a
                      (proof-list-cons-v1
                        node-variable-c
                        (proof-list-cons-v1
                          node-different-x-y
                          (proof-list-cons-v1
                            node-apart-a-c
                            (proof-list-cons-v1
                            node-given-x
                            (proof-list-cons-v1
                              node-given-y
                              (proof-list-cons-v1
                                node-given-essential
                                (proof-list-cons-v1
                                  node-lookup-y-tail
                                  (proof-list-cons-v1
                                    node-lookup-y
                                    (proof-list-cons-v1
                                      node-lookup-x
                                      (proof-list-cons-v1
                                        node-instantiate-nil
                                        (proof-list-cons-v1
                                          node-append-empty
                                          (proof-list-cons-v1
                                            node-append-c
                                            (proof-list-cons-v1
                                              node-instantiate-y
                                              (proof-list-cons-v1
                                                node-append-to-c
                                                (proof-list-cons-v1
                                                  node-append-a-c
                                                  (proof-list-cons-v1
                                                    node-instantiate-x-y
                                                    (proof-list-cons-v1
                                                      node-instantiate-literal
                                                      (proof-list-cons-v1
                                                        node-pair-a-c
                                                        (proof-list-cons-v1
                                                          node-against-nil
                                                          (proof-list-cons-v1
                                                            node-against-c
                                                            (proof-list-cons-v1
                                                              node-support-nil
                                                              (proof-list-cons-v1
                                                                node-support-a-c
                                                                (proof-list-cons-v1
                                                                  node-build-environment-nil
                                                                  (proof-list-cons-v1
                                                                    node-build-environment-y
                                                                    (proof-list-cons-v1
                                                                      node-build-environment-x
                                                                      (proof-list-cons-v1
                                                                        node-check-essentials-nil
                                                                        (proof-list-cons-v1
                                                                          node-check-essentials
                                                                          (proof-list-cons-v1
                                                                            node-check-disjoints-nil
                                                                            (proof-list-cons-v1
                                                                              node-check-disjoints
                                                                              (proof-list-cons-v1
                                                                  node-apply
                                                                  proof-list-nil-v1))))))))))))))))))))))))))))))))))))
          (body))
    (rule
          sequence-canary-article-document
          (head
            (source-proof-article-document-v1
              SequenceProofV1
              sequence-splice-apart-canary
              (proof-list-cons-v1
                (source-proof-term-ref-v1 assertion-declared)
                proof-list-nil-v1)
              node-literal
              node-apply
              (source-proof-term-ref-v1 goal)
              proof-require-rooted-v1))
          (body))
  ))
