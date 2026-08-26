; A fixed proof calculus for applying sequence assertions.  Assertion
; declarations are chronological data in the article context; they never
; elaborate to new trusted rule schemas.  The same finite rule set checks
; floating hypotheses, essential hypotheses, disjointness, and conclusion
; instantiation for every declaration.

(gslt-presentation-v1 CertificateGSLTSequenceAssertionV1
  (signature
    (operator source-proof-sequence-assertion-layer 3)
    (operator source-proof-sequence-assertion-constructor-role 4)
    (operator source-proof-sequence-assertion-judgment-role 3)
    (operator source-proof-sequence-assertion-rule-role 3)
  )
  (equations)
  (rewrites
    (rule proof-sequence-assertion-constructor-assertion
      (head
        (source-proof-calculus-constructor
          ?owner ProofAssertionV1
          (proof-succ-v1
            (proof-succ-v1
              (proof-succ-v1
                (proof-succ-v1
                  (proof-succ-v1 proof-zero-v1)))))))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)
        (source-proof-extension ?owner ?base)))
    (rule proof-sequence-assertion-constructor-variable
      (head
        (source-proof-calculus-constructor
          ?owner ProofAssertionVariableV1
          (proof-succ-v1 (proof-succ-v1 proof-zero-v1))))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)
        (source-proof-extension ?owner ?base)))
    (rule proof-sequence-assertion-constructor-essential
      (head
        (source-proof-calculus-constructor
          ?owner ProofAssertionEssentialV1
          (proof-succ-v1 proof-zero-v1)))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)
        (source-proof-extension ?owner ?base)))
    (rule proof-sequence-assertion-constructor-disjoint
      (head
        (source-proof-calculus-constructor
          ?owner ProofAssertionDisjointV1
          (proof-succ-v1 (proof-succ-v1 proof-zero-v1))))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)
        (source-proof-extension ?owner ?base)))
    (rule proof-sequence-assertion-constructor-list-nil
      (head
        (source-proof-calculus-constructor
          ?owner ProofAssertionListNilV1 proof-zero-v1))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)
        (source-proof-extension ?owner ?base)))
    (rule proof-sequence-assertion-constructor-list-cons
      (head
        (source-proof-calculus-constructor
          ?owner ProofAssertionListConsV1
          (proof-succ-v1 (proof-succ-v1 proof-zero-v1))))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)
        (source-proof-extension ?owner ?base)))

    (rule proof-sequence-assertion-judgment-declared
      (head
        (source-proof-judgment
          ?owner ProofAssertionDeclaredV1
          (proof-succ-v1 proof-zero-v1)))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)))
    (rule proof-sequence-assertion-judgment-build-environment
      (head
        (source-proof-judgment
          ?owner ProofAssertionBuildEnvironmentV1
          (proof-succ-v1
            (proof-succ-v1 (proof-succ-v1 proof-zero-v1)))))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)))
    (rule proof-sequence-assertion-judgment-check-essentials
      (head
        (source-proof-judgment
          ?owner ProofAssertionCheckEssentialsV1
          (proof-succ-v1
            (proof-succ-v1 (proof-succ-v1 proof-zero-v1)))))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)))
    (rule proof-sequence-assertion-judgment-check-disjoints
      (head
        (source-proof-judgment
          ?owner ProofAssertionCheckDisjointsV1
          (proof-succ-v1 (proof-succ-v1 proof-zero-v1))))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)))

    (rule proof-sequence-assertion-rule-use-premise
      (head
        (source-proof-rule
          ?owner proof-assertion-use-premise-v1
          (proof-list-cons-v1
            (proof-formal-v1 Formula proof-zero-v1)
            proof-list-nil-v1)
          (proof-list-cons-v1
            (proof-apply-v1 ?provable
              (proof-list-cons-v1
                (proof-fvar-v1 Formula) proof-list-nil-v1))
            proof-list-nil-v1)
          (proof-apply-v1 ?provable
            (proof-list-cons-v1
              (proof-fvar-v1 Formula) proof-list-nil-v1))
          proof-list-nil-v1))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)
        (source-proof-judgment
          ?owner ?provable (proof-succ-v1 proof-zero-v1))))

    (rule proof-sequence-assertion-rule-build-environment-nil
      (head
        (source-proof-rule
          ?owner proof-assertion-build-environment-nil-v1
          proof-list-nil-v1
          proof-list-nil-v1
          (proof-apply-v1 ProofAssertionBuildEnvironmentV1
            (proof-list-cons-v1
              (proof-apply-v1
                ProofAssertionListNilV1 proof-list-nil-v1)
              (proof-list-cons-v1
                (proof-apply-v1
                  ProofAssertionListNilV1 proof-list-nil-v1)
                (proof-list-cons-v1
                  (proof-apply-v1 ?environment-nil proof-list-nil-v1)
                  proof-list-nil-v1))))
          proof-list-nil-v1))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)
        (source-proof-sequence-layer ?owner ?base)
        (source-proof-sequence-constructor-role
          ?owner ?base environment-nil-v1 ?environment-nil)))

    (rule proof-sequence-assertion-rule-build-environment-cons
      (head
        (source-proof-rule
          ?owner proof-assertion-build-environment-cons-v1
          (proof-list-cons-v1
            (proof-formal-v1 Variable proof-zero-v1)
            (proof-list-cons-v1
              (proof-formal-v1 Typecode proof-zero-v1)
              (proof-list-cons-v1
                (proof-formal-v1 Image proof-zero-v1)
                (proof-list-cons-v1
                  (proof-formal-v1 VariablesRest proof-zero-v1)
                  (proof-list-cons-v1
                    (proof-formal-v1 ImagesRest proof-zero-v1)
                    (proof-list-cons-v1
                      (proof-formal-v1 EnvironmentRest proof-zero-v1)
                      proof-list-nil-v1))))))
          (proof-list-cons-v1
            (proof-apply-v1 ?variable-judgment
              (proof-list-cons-v1
                (proof-fvar-v1 Variable) proof-list-nil-v1))
            (proof-list-cons-v1
              (proof-apply-v1 ?provable
                (proof-list-cons-v1
                  (proof-apply-v1 ?sequence-cons
                    (proof-list-cons-v1
                      (proof-fvar-v1 Typecode)
                      (proof-list-cons-v1
                        (proof-fvar-v1 Image) proof-list-nil-v1)))
                  proof-list-nil-v1))
              (proof-list-cons-v1
                (proof-apply-v1 ProofAssertionBuildEnvironmentV1
                  (proof-list-cons-v1
                    (proof-fvar-v1 VariablesRest)
                    (proof-list-cons-v1
                      (proof-fvar-v1 ImagesRest)
                      (proof-list-cons-v1
                        (proof-fvar-v1 EnvironmentRest)
                        proof-list-nil-v1))))
                proof-list-nil-v1)))
          (proof-apply-v1 ProofAssertionBuildEnvironmentV1
            (proof-list-cons-v1
              (proof-apply-v1 ProofAssertionListConsV1
                (proof-list-cons-v1
                  (proof-apply-v1 ProofAssertionVariableV1
                    (proof-list-cons-v1
                      (proof-fvar-v1 Variable)
                      (proof-list-cons-v1
                        (proof-fvar-v1 Typecode) proof-list-nil-v1)))
                  (proof-list-cons-v1
                    (proof-fvar-v1 VariablesRest) proof-list-nil-v1)))
              (proof-list-cons-v1
                (proof-apply-v1 ProofAssertionListConsV1
                  (proof-list-cons-v1
                    (proof-fvar-v1 Image)
                    (proof-list-cons-v1
                      (proof-fvar-v1 ImagesRest) proof-list-nil-v1)))
                (proof-list-cons-v1
                  (proof-apply-v1 ?environment-cons
                    (proof-list-cons-v1
                      (proof-fvar-v1 Variable)
                      (proof-list-cons-v1
                        (proof-fvar-v1 Image)
                        (proof-list-cons-v1
                          (proof-fvar-v1 EnvironmentRest)
                          proof-list-nil-v1))))
                  proof-list-nil-v1))))
          proof-list-nil-v1))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)
        (source-proof-sequence-layer ?owner ?base)
        (source-proof-sequence-constructor-role
          ?owner ?base sequence-cons-v1 ?sequence-cons)
        (source-proof-sequence-constructor-role
          ?owner ?base environment-cons-v1 ?environment-cons)
        (source-proof-sequence-judgment-role
          ?owner variable-v1 ?variable-judgment)))

    (rule proof-sequence-assertion-rule-check-essentials-nil
      (head
        (source-proof-rule
          ?owner proof-assertion-check-essentials-nil-v1
          (proof-list-cons-v1
            (proof-formal-v1 Environment proof-zero-v1)
            proof-list-nil-v1)
          proof-list-nil-v1
          (proof-apply-v1 ProofAssertionCheckEssentialsV1
            (proof-list-cons-v1
              (proof-apply-v1
                ProofAssertionListNilV1 proof-list-nil-v1)
              (proof-list-cons-v1
                (proof-apply-v1
                  ProofAssertionListNilV1 proof-list-nil-v1)
                (proof-list-cons-v1
                  (proof-fvar-v1 Environment) proof-list-nil-v1))))
          proof-list-nil-v1))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)))

    (rule proof-sequence-assertion-rule-check-essentials-cons
      (head
        (source-proof-rule
          ?owner proof-assertion-check-essentials-cons-v1
          (proof-list-cons-v1
            (proof-formal-v1 Template proof-zero-v1)
            (proof-list-cons-v1
              (proof-formal-v1 Actual proof-zero-v1)
              (proof-list-cons-v1
                (proof-formal-v1 EssentialsRest proof-zero-v1)
                (proof-list-cons-v1
                  (proof-formal-v1 ActualsRest proof-zero-v1)
                  (proof-list-cons-v1
                    (proof-formal-v1 Environment proof-zero-v1)
                    proof-list-nil-v1)))))
          (proof-list-cons-v1
            (proof-apply-v1 ?provable
              (proof-list-cons-v1
                (proof-fvar-v1 Actual) proof-list-nil-v1))
            (proof-list-cons-v1
              (proof-apply-v1 ?instantiate
                (proof-list-cons-v1
                  (proof-fvar-v1 Template)
                  (proof-list-cons-v1
                    (proof-fvar-v1 Environment)
                    (proof-list-cons-v1
                      (proof-fvar-v1 Actual) proof-list-nil-v1))))
              (proof-list-cons-v1
                (proof-apply-v1 ProofAssertionCheckEssentialsV1
                  (proof-list-cons-v1
                    (proof-fvar-v1 EssentialsRest)
                    (proof-list-cons-v1
                      (proof-fvar-v1 ActualsRest)
                      (proof-list-cons-v1
                        (proof-fvar-v1 Environment)
                        proof-list-nil-v1))))
                proof-list-nil-v1)))
          (proof-apply-v1 ProofAssertionCheckEssentialsV1
            (proof-list-cons-v1
              (proof-apply-v1 ProofAssertionListConsV1
                (proof-list-cons-v1
                  (proof-apply-v1 ProofAssertionEssentialV1
                    (proof-list-cons-v1
                      (proof-fvar-v1 Template) proof-list-nil-v1))
                  (proof-list-cons-v1
                    (proof-fvar-v1 EssentialsRest) proof-list-nil-v1)))
              (proof-list-cons-v1
                (proof-apply-v1 ProofAssertionListConsV1
                  (proof-list-cons-v1
                    (proof-fvar-v1 Actual)
                    (proof-list-cons-v1
                      (proof-fvar-v1 ActualsRest) proof-list-nil-v1)))
                (proof-list-cons-v1
                  (proof-fvar-v1 Environment) proof-list-nil-v1))))
          proof-list-nil-v1))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)
        (source-proof-sequence-judgment-role
          ?owner instantiate-v1 ?instantiate)))

    (rule proof-sequence-assertion-rule-check-disjoints-nil
      (head
        (source-proof-rule
          ?owner proof-assertion-check-disjoints-nil-v1
          (proof-list-cons-v1
            (proof-formal-v1 Environment proof-zero-v1)
            proof-list-nil-v1)
          proof-list-nil-v1
          (proof-apply-v1 ProofAssertionCheckDisjointsV1
            (proof-list-cons-v1
              (proof-apply-v1
                ProofAssertionListNilV1 proof-list-nil-v1)
              (proof-list-cons-v1
                (proof-fvar-v1 Environment) proof-list-nil-v1)))
          proof-list-nil-v1))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)))

    (rule proof-sequence-assertion-rule-check-disjoints-cons
      (head
        (source-proof-rule
          ?owner proof-assertion-check-disjoints-cons-v1
          (proof-list-cons-v1
            (proof-formal-v1 Left proof-zero-v1)
            (proof-list-cons-v1
              (proof-formal-v1 Right proof-zero-v1)
              (proof-list-cons-v1
                (proof-formal-v1 DisjointsRest proof-zero-v1)
                (proof-list-cons-v1
                  (proof-formal-v1 Environment proof-zero-v1)
                  (proof-list-cons-v1
                    (proof-formal-v1 LeftImage proof-zero-v1)
                    (proof-list-cons-v1
                      (proof-formal-v1 RightImage proof-zero-v1)
                      proof-list-nil-v1))))))
          (proof-list-cons-v1
            (proof-apply-v1 ?different
              (proof-list-cons-v1
                (proof-fvar-v1 Left)
                (proof-list-cons-v1
                  (proof-fvar-v1 Right) proof-list-nil-v1)))
            (proof-list-cons-v1
              (proof-apply-v1 ?lookup
                (proof-list-cons-v1
                  (proof-fvar-v1 Environment)
                  (proof-list-cons-v1
                    (proof-fvar-v1 Left)
                    (proof-list-cons-v1
                      (proof-fvar-v1 LeftImage) proof-list-nil-v1))))
              (proof-list-cons-v1
                (proof-apply-v1 ?lookup
                  (proof-list-cons-v1
                    (proof-fvar-v1 Environment)
                    (proof-list-cons-v1
                      (proof-fvar-v1 Right)
                      (proof-list-cons-v1
                        (proof-fvar-v1 RightImage)
                        proof-list-nil-v1))))
                (proof-list-cons-v1
                  (proof-apply-v1 ?support-apart
                    (proof-list-cons-v1
                      (proof-fvar-v1 LeftImage)
                      (proof-list-cons-v1
                        (proof-fvar-v1 RightImage)
                        proof-list-nil-v1)))
                  (proof-list-cons-v1
                    (proof-apply-v1 ProofAssertionCheckDisjointsV1
                      (proof-list-cons-v1
                        (proof-fvar-v1 DisjointsRest)
                        (proof-list-cons-v1
                          (proof-fvar-v1 Environment)
                          proof-list-nil-v1)))
                    proof-list-nil-v1)))))
          (proof-apply-v1 ProofAssertionCheckDisjointsV1
            (proof-list-cons-v1
              (proof-apply-v1 ProofAssertionListConsV1
                (proof-list-cons-v1
                  (proof-apply-v1 ProofAssertionDisjointV1
                    (proof-list-cons-v1
                      (proof-fvar-v1 Left)
                      (proof-list-cons-v1
                        (proof-fvar-v1 Right) proof-list-nil-v1)))
                  (proof-list-cons-v1
                    (proof-fvar-v1 DisjointsRest) proof-list-nil-v1)))
              (proof-list-cons-v1
                (proof-fvar-v1 Environment) proof-list-nil-v1)))
          proof-list-nil-v1))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)
        (source-proof-sequence-judgment-role
          ?owner different-v1 ?different)
        (source-proof-sequence-judgment-role
          ?owner lookup-v1 ?lookup)
        (source-proof-sequence-judgment-role
          ?owner support-apart-v1 ?support-apart)))

    (rule proof-sequence-assertion-rule-apply
      (head
        (source-proof-rule
          ?owner proof-assertion-apply-v1
          (proof-list-cons-v1
            (proof-formal-v1 Variables proof-zero-v1)
            (proof-list-cons-v1
              (proof-formal-v1 Essentials proof-zero-v1)
              (proof-list-cons-v1
                (proof-formal-v1 ConclusionType proof-zero-v1)
                (proof-list-cons-v1
                  (proof-formal-v1 ConclusionTemplate proof-zero-v1)
                  (proof-list-cons-v1
                    (proof-formal-v1 Disjoints proof-zero-v1)
                    (proof-list-cons-v1
                      (proof-formal-v1 Images proof-zero-v1)
                      (proof-list-cons-v1
                        (proof-formal-v1 Actuals proof-zero-v1)
                        (proof-list-cons-v1
                          (proof-formal-v1 Environment proof-zero-v1)
                          (proof-list-cons-v1
                            (proof-formal-v1 Result proof-zero-v1)
                            proof-list-nil-v1)))))))))
          (proof-list-cons-v1
            (proof-apply-v1 ProofAssertionDeclaredV1
              (proof-list-cons-v1
                (proof-apply-v1 ProofAssertionV1
                  (proof-list-cons-v1
                    (proof-fvar-v1 Variables)
                    (proof-list-cons-v1
                      (proof-fvar-v1 Essentials)
                      (proof-list-cons-v1
                        (proof-fvar-v1 ConclusionType)
                        (proof-list-cons-v1
                          (proof-fvar-v1 ConclusionTemplate)
                          (proof-list-cons-v1
                            (proof-fvar-v1 Disjoints)
                            proof-list-nil-v1))))))
                proof-list-nil-v1))
            (proof-list-cons-v1
              (proof-apply-v1 ProofAssertionBuildEnvironmentV1
                (proof-list-cons-v1
                  (proof-fvar-v1 Variables)
                  (proof-list-cons-v1
                    (proof-fvar-v1 Images)
                    (proof-list-cons-v1
                      (proof-fvar-v1 Environment) proof-list-nil-v1))))
              (proof-list-cons-v1
                (proof-apply-v1 ProofAssertionCheckEssentialsV1
                  (proof-list-cons-v1
                    (proof-fvar-v1 Essentials)
                    (proof-list-cons-v1
                      (proof-fvar-v1 Actuals)
                      (proof-list-cons-v1
                        (proof-fvar-v1 Environment) proof-list-nil-v1))))
                (proof-list-cons-v1
                  (proof-apply-v1 ProofAssertionCheckDisjointsV1
                    (proof-list-cons-v1
                      (proof-fvar-v1 Disjoints)
                      (proof-list-cons-v1
                        (proof-fvar-v1 Environment) proof-list-nil-v1)))
                  (proof-list-cons-v1
                    (proof-apply-v1 ?instantiate
                      (proof-list-cons-v1
                        (proof-fvar-v1 ConclusionTemplate)
                        (proof-list-cons-v1
                          (proof-fvar-v1 Environment)
                          (proof-list-cons-v1
                            (proof-fvar-v1 Result)
                            proof-list-nil-v1))))
                    proof-list-nil-v1)))))
          (proof-apply-v1 ?provable
            (proof-list-cons-v1
              (proof-apply-v1 ?sequence-cons
                (proof-list-cons-v1
                  (proof-fvar-v1 ConclusionType)
                  (proof-list-cons-v1
                    (proof-fvar-v1 Result) proof-list-nil-v1)))
              proof-list-nil-v1))
          proof-list-nil-v1))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)
        (source-proof-sequence-layer ?owner ?base)
        (source-proof-judgment
          ?owner ?provable (proof-succ-v1 proof-zero-v1))
        (source-proof-sequence-constructor-role
          ?owner ?base sequence-cons-v1 ?sequence-cons)
        (source-proof-sequence-judgment-role
          ?owner instantiate-v1 ?instantiate)))

    (rule proof-sequence-assertion-role-constructor-assertion
      (head
        (source-proof-sequence-assertion-constructor-role
          ?owner ?base assertion-v1 ProofAssertionV1))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)))
    (rule proof-sequence-assertion-role-constructor-variable
      (head
        (source-proof-sequence-assertion-constructor-role
          ?owner ?base variable-v1 ProofAssertionVariableV1))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)))
    (rule proof-sequence-assertion-role-constructor-essential
      (head
        (source-proof-sequence-assertion-constructor-role
          ?owner ?base essential-v1 ProofAssertionEssentialV1))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)))
    (rule proof-sequence-assertion-role-constructor-disjoint
      (head
        (source-proof-sequence-assertion-constructor-role
          ?owner ?base disjoint-v1 ProofAssertionDisjointV1))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)))
    (rule proof-sequence-assertion-role-constructor-list-nil
      (head
        (source-proof-sequence-assertion-constructor-role
          ?owner ?base list-nil-v1 ProofAssertionListNilV1))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)))
    (rule proof-sequence-assertion-role-constructor-list-cons
      (head
        (source-proof-sequence-assertion-constructor-role
          ?owner ?base list-cons-v1 ProofAssertionListConsV1))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)))

    (rule proof-sequence-assertion-role-judgment-declared
      (head
        (source-proof-sequence-assertion-judgment-role
          ?owner declared-v1 ProofAssertionDeclaredV1))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)))
    (rule proof-sequence-assertion-role-judgment-build-environment
      (head
        (source-proof-sequence-assertion-judgment-role
          ?owner build-environment-v1
          ProofAssertionBuildEnvironmentV1))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)))
    (rule proof-sequence-assertion-role-judgment-check-essentials
      (head
        (source-proof-sequence-assertion-judgment-role
          ?owner check-essentials-v1
          ProofAssertionCheckEssentialsV1))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)))
    (rule proof-sequence-assertion-role-judgment-check-disjoints
      (head
        (source-proof-sequence-assertion-judgment-role
          ?owner check-disjoints-v1
          ProofAssertionCheckDisjointsV1))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)))
    (rule proof-sequence-assertion-role-judgment-provable
      (head
        (source-proof-sequence-assertion-judgment-role
          ?owner provable-v1 ?provable))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)
        (source-proof-judgment
          ?owner ?provable (proof-succ-v1 proof-zero-v1))))

    (rule proof-sequence-assertion-role-rule-build-environment-nil
      (head
        (source-proof-sequence-assertion-rule-role
          ?owner build-environment-nil-v1
          proof-assertion-build-environment-nil-v1))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)))
    (rule proof-sequence-assertion-role-rule-build-environment-cons
      (head
        (source-proof-sequence-assertion-rule-role
          ?owner build-environment-cons-v1
          proof-assertion-build-environment-cons-v1))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)))
    (rule proof-sequence-assertion-role-rule-check-essentials-nil
      (head
        (source-proof-sequence-assertion-rule-role
          ?owner check-essentials-nil-v1
          proof-assertion-check-essentials-nil-v1))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)))
    (rule proof-sequence-assertion-role-rule-check-essentials-cons
      (head
        (source-proof-sequence-assertion-rule-role
          ?owner check-essentials-cons-v1
          proof-assertion-check-essentials-cons-v1))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)))
    (rule proof-sequence-assertion-role-rule-check-disjoints-nil
      (head
        (source-proof-sequence-assertion-rule-role
          ?owner check-disjoints-nil-v1
          proof-assertion-check-disjoints-nil-v1))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)))
    (rule proof-sequence-assertion-role-rule-check-disjoints-cons
      (head
        (source-proof-sequence-assertion-rule-role
          ?owner check-disjoints-cons-v1
          proof-assertion-check-disjoints-cons-v1))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)))
    (rule proof-sequence-assertion-role-rule-apply
      (head
        (source-proof-sequence-assertion-rule-role
          ?owner apply-v1 proof-assertion-apply-v1))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)))
    (rule proof-sequence-assertion-role-rule-use-premise
      (head
        (source-proof-sequence-assertion-rule-role
          ?owner use-premise-v1 proof-assertion-use-premise-v1))
      (body
        (source-proof-sequence-assertion-layer ?owner ?base ?provable)))
  ))
