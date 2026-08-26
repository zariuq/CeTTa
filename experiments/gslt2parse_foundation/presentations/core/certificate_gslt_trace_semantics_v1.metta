; Generic chronological execution for sequence-proof traces.  The ordered
; frame is consumed from its tail because the last mandatory hypothesis is
; the topmost stack entry.  Popped entries already carry trace provenance;
; this layer checks their shapes, substitution, essential instantiation, and
; apartness obligations without re-deriving them as ambient theorems.

(gslt-presentation-v1 ProofGSLTTraceSemanticsV1
  (signature
    (operator ProofTraceAcceptedV1 3)
    (operator ProofTraceApplyDepthResultV1 8)
    (operator ProofTraceApplyExecutionResultV1 6)
    (operator ProofTraceApplyAssertionV1 4)
    (operator ProofTraceApplyAssertionResultV1 4)
    (operator ProofTraceRealizeAssertionV1 4)
    (operator ProofTraceRealizeAssertionResultV1 4)
    (operator ProofTraceRealizeDisjointsResultV1 6)
    (operator ProofTraceRealizeEssentialsResultV1 7)
    (operator ProofTraceRealizeFrameResultV1 7)
    (operator ProofTraceRealizeSucceededV1 1)
    (operator ProofTraceRealizeRefusedV1 1)
    (operator ProofTraceApplyV1 2)
    (operator ProofTraceBuildEnvironmentV1 3)
    (operator ProofTraceCheckEssentialsV1 3)
    (operator ProofTraceCheckEssentialsResultV1 4)
    (operator ProofTraceCheckExecutionResultV1 3)
    (operator ProofTraceCheckResultV1 4)
    (operator ProofTraceConsV1 2)
    (operator ProofTraceContextConsV1 2)
    (operator ProofTraceContextMemberV1 2)
    (operator ProofTraceContextMemberComparedV1 3)
    (operator ProofTraceExecV1 4)
    (operator ProofTraceExecStateV1 6)
    (operator ProofTraceExecStateResultV1 5)
    (operator ProofTraceExecSucceededV1 2)
    (operator ProofTraceExecRefusedV1 3)
    (operator ProofTraceFinalResultV1 3)
    (operator ProofTraceFinalizeCompareV1 4)
    (operator ProofTraceFrameApplyV1 5)
    (operator ProofTraceFrameApplyResultV1 3)
    (operator ProofTraceFrameAppliedV1 3)
    (operator ProofTraceFrameApplyEssentialResultV1 2)
    (operator ProofTraceFrameApplyVariableResultV1 3)
    (operator ProofTraceFrameApplyVariableComparedV1 8)
    (operator ProofTraceFrameConsV1 2)
    (operator ProofTraceFrameEssentialV1 1)
    (operator ProofTraceFrameProjectV1 3)
    (operator ProofTraceFrameStackDepthV1 3)
    (operator ProofTraceFrameVariableV1 2)
    (operator ProofTraceIncompleteStepV1 1)
    (operator ProofTraceNatSuccV1 1)
    (operator ProofTracePushV1 1)
    (operator ProofTraceRestoreV1 1)
    (operator ProofTraceSavedAppendV1 3)
    (operator ProofTraceSavedConsV1 2)
    (operator ProofTraceSavedFoundV1 1)
    (operator ProofTraceSavedLookupV1 3)
    (operator ProofTraceSavedLookupFromV1 4)
    (operator ProofTraceSavedLookupResultV1 3)
    (operator ProofTraceSavedMissingV1 1)
    (operator ProofTraceRestoreLookupExecutionResultV1 6)
    (operator ProofTraceCheckDisjointHeadResultV1 6)
    (operator ProofTraceCheckDisjointsResultV1 3)
    (operator ProofTracePairConflictV1 2)
    (operator ProofTraceSequenceSupportApartHeadResultV1 4)
    (operator ProofTraceSequenceSupportApartResultV1 3)
    (operator ProofTraceTokenAgainstSequenceHeadResultV1 4)
    (operator ProofTraceTokenAgainstSequenceResultV1 3)
    (operator ProofTraceTokenPairApartDecisionV1 4)
    (operator ProofTraceTokenPairComparedV1 4)
    (operator ProofTraceTokenPairDecisionV1 3)
    (operator ProofTraceTokenPairLeftClassV1 4)
    (operator ProofTraceTokenPairRightClassV1 4)
    (operator ProofTokenApartDecisionV1 3)
    (operator ProofTokenClassV1 2)
    (operator ProofTokenCompareV1 3)
    (operator ProofSequenceCompareV1 3)
    (operator ProofSequenceCompareHeadsV1 4)
    (operator ProofTraceStackConsV1 2)
    (operator ProofTraceRefusedClaimMismatchV1 2)
    (operator ProofTraceRefusedDisjointV1 4)
    (operator ProofTraceRefusedEssentialMismatchV1 3)
    (operator ProofTraceRefusedExtraStackV1 1)
    (operator ProofTraceRefusedExplicitMandatoryHypothesisV1 1)
    (operator ProofTraceRefusedInactiveHypothesisV1 1)
    (operator ProofTraceRefusedSaveDuringIndexV1 1)
    (operator ProofTraceRefusedSavePlacementV1 1)
    (operator ProofTraceRefusedUnknownLabelV1 1)
    (operator ProofTraceRefusedStackUnderflowV1 2)
    (operator ProofTraceRefusedSavedStepOutOfRangeV1 1)
    (operator ProofTraceRefusedTypecodeMismatchV1 2)
    (operator ProofTraceRefusalClassV1 2)
    (operator ProofTraceResultIsRefusalV1 1)
  )
  (equations)
  (rewrites
    (rule proof-trace-context-member-v1
      (head
        (ProofTraceContextMemberV1
          (ProofTraceContextConsV1 ?head ?rest) ?formula))
      (body
        (ProofSequenceCompareV1 ?head ?formula ?comparison)
        (ProofTraceContextMemberComparedV1
          ?comparison ?rest ?formula)))
    (rule proof-trace-context-member-compared-equal-v1
      (head
        (ProofTraceContextMemberComparedV1
          ProofSequenceEqualV1 ?rest ?formula))
      (body))
    (rule proof-trace-context-member-compared-different-v1
      (head
        (ProofTraceContextMemberComparedV1
          ProofSequenceDifferentV1 ?rest ?formula))
      (body
        (ProofTraceContextMemberV1 ?rest ?formula)))

    (rule proof-trace-frame-project-nil-v1
      (head
        (ProofTraceFrameProjectV1
          ProofTraceFrameNilV1
          ProofAssertionListNilV1
          ProofAssertionListNilV1))
      (body))
    (rule proof-trace-frame-project-variable-v1
      (head
        (ProofTraceFrameProjectV1
          (ProofTraceFrameConsV1
            (ProofTraceFrameVariableV1 ?variable ?typecode) ?rest)
          (ProofAssertionListConsV1
            (ProofAssertionVariableV1 ?variable ?typecode) ?variables)
          ?essentials))
      (body
        (ProofTraceFrameProjectV1 ?rest ?variables ?essentials)))
    (rule proof-trace-frame-project-essential-v1
      (head
        (ProofTraceFrameProjectV1
          (ProofTraceFrameConsV1
            (ProofTraceFrameEssentialV1 ?template) ?rest)
          ?variables
          (ProofAssertionListConsV1
            (ProofAssertionEssentialV1 ?template) ?essentials)))
      (body
        (ProofTraceFrameProjectV1 ?rest ?variables ?essentials)))

    (rule proof-trace-frame-apply-nil-v1
      (head
        (ProofTraceFrameApplyV1
          ProofTraceFrameNilV1 ?stack
          ProofAssertionListNilV1 ProofAssertionListNilV1 ?stack))
      (body))
    (rule proof-trace-frame-apply-variable-v1
      (head
        (ProofTraceFrameApplyV1
          (ProofTraceFrameConsV1
            (ProofTraceFrameVariableV1 ?variable ?typecode) ?rest)
          ?stack
          (ProofAssertionListConsV1 ?image ?images)
          ?actuals ?output))
      (body
        (ProofTraceFrameApplyV1
          ?rest ?stack ?images ?actuals
          (ProofTraceStackConsV1
            (ProofSequenceConsV1 ?typecode ?image) ?output))))
    (rule proof-trace-frame-apply-essential-v1
      (head
        (ProofTraceFrameApplyV1
          (ProofTraceFrameConsV1
            (ProofTraceFrameEssentialV1 ?template) ?rest)
          ?stack ?images
          (ProofAssertionListConsV1 ?actual ?actuals) ?output))
      (body
        (ProofTraceFrameApplyV1
          ?rest ?stack ?images ?actuals
          (ProofTraceStackConsV1 ?actual ?output))))

    ; The success projection above retains the original relational API.  This
    ; result-producing form makes its typecode side condition total: a
    ; mandatory variable consumes a stack formula only when the formula's
    ; leading token is the authored typecode, otherwise the expected and
    ; actual tokens are retained in a refusal witness.
    (rule proof-trace-frame-apply-result-nil-v1
      (head
        (ProofTraceFrameApplyResultV1
          ProofTraceFrameNilV1 ?stack
          (ProofTraceFrameAppliedV1
            ProofAssertionListNilV1 ProofAssertionListNilV1 ?stack)))
      (body))
    (rule proof-trace-frame-apply-result-variable-v1
      (head
        (ProofTraceFrameApplyResultV1
          (ProofTraceFrameConsV1
            (ProofTraceFrameVariableV1 ?variable ?typecode) ?rest)
          ?stack ?result))
      (body
        (ProofTraceFrameApplyResultV1 ?rest ?stack ?tail-result)
        (ProofTraceFrameApplyVariableResultV1
          ?typecode ?tail-result ?result)))
    (rule proof-trace-frame-apply-result-essential-v1
      (head
        (ProofTraceFrameApplyResultV1
          (ProofTraceFrameConsV1
            (ProofTraceFrameEssentialV1 ?template) ?rest)
          ?stack ?result))
      (body
        (ProofTraceFrameApplyResultV1 ?rest ?stack ?tail-result)
        (ProofTraceFrameApplyEssentialResultV1
          ?tail-result ?result)))
    (rule proof-trace-frame-apply-essential-applied-v1
      (head
        (ProofTraceFrameApplyEssentialResultV1
          (ProofTraceFrameAppliedV1
            ?images ?actuals
            (ProofTraceStackConsV1 ?actual ?output))
          (ProofTraceFrameAppliedV1
            ?images
            (ProofAssertionListConsV1 ?actual ?actuals)
            ?output)))
      (body))
    (rule proof-trace-frame-apply-essential-refused-v1
      (head
        (ProofTraceFrameApplyEssentialResultV1
          (ProofTraceRealizeRefusedV1 ?reason)
          (ProofTraceRealizeRefusedV1 ?reason)))
      (body))
    (rule proof-trace-frame-apply-variable-applied-v1
      (head
        (ProofTraceFrameApplyVariableResultV1
          ?expected-typecode
          (ProofTraceFrameAppliedV1
            ?images ?actuals
            (ProofTraceStackConsV1
              (ProofSequenceConsV1 ?actual-typecode ?image) ?output))
          ?result))
      (body
        (ProofTokenCompareV1
          ?expected-typecode ?actual-typecode ?comparison)
        (ProofTraceFrameApplyVariableComparedV1
          ?comparison ?expected-typecode ?actual-typecode
          ?image ?images ?actuals ?output ?result)))
    (rule proof-trace-frame-apply-variable-missing-typecode-v1
      (head
        (ProofTraceFrameApplyVariableResultV1
          ?expected-typecode
          (ProofTraceFrameAppliedV1
            ?images ?actuals
            (ProofTraceStackConsV1 ProofSequenceNilV1 ?output))
          (ProofTraceRealizeRefusedV1
            (ProofTraceRefusedTypecodeMismatchV1
              ?expected-typecode ProofTraceMissingTypecodeV1))))
      (body))
    (rule proof-trace-frame-apply-variable-refused-v1
      (head
        (ProofTraceFrameApplyVariableResultV1
          ?expected-typecode
          (ProofTraceRealizeRefusedV1 ?reason)
          (ProofTraceRealizeRefusedV1 ?reason)))
      (body))
    (rule proof-trace-frame-apply-variable-equal-v1
      (head
        (ProofTraceFrameApplyVariableComparedV1
          ProofTokenEqualV1 ?expected-typecode ?actual-typecode
          ?image ?images ?actuals ?output
          (ProofTraceFrameAppliedV1
            (ProofAssertionListConsV1 ?image ?images)
            ?actuals ?output)))
      (body))
    (rule proof-trace-frame-apply-variable-different-v1
      (head
        (ProofTraceFrameApplyVariableComparedV1
          ProofTokenUnequalV1 ?expected-typecode ?actual-typecode
          ?image ?images ?actuals ?output
          (ProofTraceRealizeRefusedV1
            (ProofTraceRefusedTypecodeMismatchV1
              ?expected-typecode ?actual-typecode))))
      (body))

    (rule proof-trace-build-environment-nil-v1
      (head
        (ProofTraceBuildEnvironmentV1
          ProofAssertionListNilV1 ProofAssertionListNilV1
          ProofEnvironmentNilV1))
      (body))
    (rule proof-trace-build-environment-cons-v1
      (head
        (ProofTraceBuildEnvironmentV1
          (ProofAssertionListConsV1
            (ProofAssertionVariableV1 ?variable ?typecode) ?variables)
          (ProofAssertionListConsV1 ?image ?images)
          (ProofEnvironmentConsV1 ?variable ?image ?environment)))
      (body
        (ProofTokenVariableV1 ?variable)
        (ProofTraceBuildEnvironmentV1
          ?variables ?images ?environment)))

    (rule proof-trace-check-essentials-nil-v1
      (head
        (ProofTraceCheckEssentialsV1
          ProofAssertionListNilV1 ProofAssertionListNilV1 ?environment))
      (body))
    (rule proof-trace-check-essentials-cons-v1
      (head
        (ProofTraceCheckEssentialsV1
          (ProofAssertionListConsV1
            (ProofAssertionEssentialV1 ?template) ?essentials)
          (ProofAssertionListConsV1 ?actual ?actuals)
          ?environment))
      (body
        (ProofSequenceInstantiateV1 ?template ?environment ?actual)
        (ProofTraceCheckEssentialsV1
          ?essentials ?actuals ?environment)))

    ; Essential checking is a total result on shape-compatible lists.  The
    ; expected instantiated formula is retained in the refusal witness, so a
    ; target backend never has to turn relational failure into proof meaning.
    (rule proof-trace-check-essentials-result-nil-v1
      (head
        (ProofTraceCheckEssentialsResultV1
          ProofAssertionListNilV1 ProofAssertionListNilV1 ?environment
          ProofTraceEssentialsSucceededV1))
      (body))
    (rule proof-trace-check-essentials-result-cons-equal-v1
      (head
        (ProofTraceCheckEssentialsResultV1
          (ProofAssertionListConsV1
            (ProofAssertionEssentialV1 ?template) ?essentials)
          (ProofAssertionListConsV1 ?actual ?actuals)
          ?environment ?result))
      (body
        (ProofSequenceInstantiateV1 ?template ?environment ?expected)
        (ProofSequenceCompareV1
          ?expected ?actual ProofSequenceEqualV1)
        (ProofTraceCheckEssentialsResultV1
          ?essentials ?actuals ?environment ?result)))
    (rule proof-trace-check-essentials-result-cons-different-v1
      (head
        (ProofTraceCheckEssentialsResultV1
          (ProofAssertionListConsV1
            (ProofAssertionEssentialV1 ?template) ?essentials)
          (ProofAssertionListConsV1 ?actual ?actuals)
          ?environment
          (ProofTraceRefusedEssentialMismatchV1
            ?template ?expected ?actual)))
      (body
        (ProofSequenceInstantiateV1 ?template ?environment ?expected)
        (ProofSequenceCompareV1
          ?expected ?actual ProofSequenceDifferentV1)))

    ; Structural assertion realization is distinct from authority to use the
    ; assertion.  The ordinary relation obtains authority from
    ; ProofAssertionDeclaredV1; source-owned adapters can retain stronger
    ; provenance while reusing exactly this realization.
    (rule proof-trace-realize-assertion-result-v1
      (head
        (ProofTraceRealizeAssertionResultV1
          (ProofAssertionV1
            ?variables ?essentials ?conclusion-type
            ?conclusion-template ?disjoints)
          ?frame ?input ?result))
      (body
        (ProofTraceFrameProjectV1 ?frame ?variables ?essentials)
        (ProofTraceFrameApplyResultV1 ?frame ?input ?frame-result)
        (ProofTraceRealizeFrameResultV1
          ?frame-result ?variables ?essentials
          ?conclusion-type ?conclusion-template ?disjoints ?result)))
    (rule proof-trace-realize-frame-applied-v1
      (head
        (ProofTraceRealizeFrameResultV1
          (ProofTraceFrameAppliedV1 ?images ?actuals ?rest)
          ?variables ?essentials ?conclusion-type
          ?conclusion-template ?disjoints ?result))
      (body
        (ProofTraceBuildEnvironmentV1
          ?variables ?images ?environment)
        (ProofTraceCheckEssentialsResultV1
          ?essentials ?actuals ?environment ?essentials-result)
        (ProofTraceRealizeEssentialsResultV1
          ?essentials-result ?conclusion-type ?conclusion-template
          ?disjoints ?environment ?rest ?result)))
    (rule proof-trace-realize-frame-refused-v1
      (head
        (ProofTraceRealizeFrameResultV1
          (ProofTraceRealizeRefusedV1 ?reason)
          ?variables ?essentials ?conclusion-type
          ?conclusion-template ?disjoints
          (ProofTraceRealizeRefusedV1 ?reason)))
      (body))
    (rule proof-trace-realize-essentials-succeeded-v1
      (head
        (ProofTraceRealizeEssentialsResultV1
          ProofTraceEssentialsSucceededV1
          ?conclusion-type ?conclusion-template ?disjoints ?environment ?rest
          ?result))
      (body
        (ProofTraceCheckDisjointsResultV1
          ?disjoints ?environment ?disjoint-result)
        (ProofTraceRealizeDisjointsResultV1
          ?disjoint-result ?conclusion-type ?conclusion-template
          ?environment ?rest ?result)))
    (rule proof-trace-realize-disjoints-succeeded-v1
      (head
        (ProofTraceRealizeDisjointsResultV1
          ProofTraceDisjointSucceededV1
          ?conclusion-type ?conclusion-template ?environment ?rest
          (ProofTraceRealizeSucceededV1
            (ProofTraceStackConsV1
              (ProofSequenceConsV1 ?conclusion-type ?result) ?rest))))
      (body
        (ProofSequenceInstantiateV1
          ?conclusion-template ?environment ?result)))
    (rule proof-trace-realize-disjoints-refused-v1
      (head
        (ProofTraceRealizeDisjointsResultV1
          (ProofTraceRefusedDisjointV1
            ?required-left ?required-right ?actual-left ?actual-right)
          ?conclusion-type ?conclusion-template ?environment ?rest
          (ProofTraceRealizeRefusedV1
            (ProofTraceRefusedDisjointV1
              ?required-left ?required-right ?actual-left ?actual-right))))
      (body))

    ; Disjoint-variable checking is a total structural decision.  Literal
    ; pairs are always admissible; two variables must be different and must
    ; occur in the source-owned current-apartness relation.  The first
    ; conflicting image pair is retained as refusal evidence.
    (rule proof-trace-token-pair-decision-v1
      (head
        (ProofTraceTokenPairDecisionV1 ?left ?right ?result))
      (body
        (ProofTokenClassV1 ?left ?left-class)
        (ProofTraceTokenPairLeftClassV1
          ?left-class ?left ?right ?result)))
    (rule proof-trace-token-pair-left-literal-v1
      (head
        (ProofTraceTokenPairLeftClassV1
          ProofTokenLiteralClassV1 ?left ?right
          ProofTracePairAllowedV1))
      (body))
    (rule proof-trace-token-pair-left-variable-v1
      (head
        (ProofTraceTokenPairLeftClassV1
          ProofTokenVariableClassV1 ?left ?right ?result))
      (body
        (ProofTokenClassV1 ?right ?right-class)
        (ProofTraceTokenPairRightClassV1
          ?right-class ?left ?right ?result)))
    (rule proof-trace-token-pair-right-literal-v1
      (head
        (ProofTraceTokenPairRightClassV1
          ProofTokenLiteralClassV1 ?left ?right
          ProofTracePairAllowedV1))
      (body))
    (rule proof-trace-token-pair-right-variable-v1
      (head
        (ProofTraceTokenPairRightClassV1
          ProofTokenVariableClassV1 ?left ?right ?result))
      (body
        (ProofTokenCompareV1 ?left ?right ?comparison)
        (ProofTraceTokenPairComparedV1
          ?comparison ?left ?right ?result)))
    (rule proof-trace-token-pair-equal-v1
      (head
        (ProofTraceTokenPairComparedV1
          ProofTokenEqualV1 ?left ?right
          (ProofTracePairConflictV1 ?left ?right)))
      (body))
    (rule proof-trace-token-pair-unequal-v1
      (head
        (ProofTraceTokenPairComparedV1
          ProofTokenUnequalV1 ?left ?right ?result))
      (body
        (ProofTokenApartDecisionV1 ?left ?right ?apart-decision)
        (ProofTraceTokenPairApartDecisionV1
          ?apart-decision ?left ?right ?result)))
    (rule proof-trace-token-pair-apart-v1
      (head
        (ProofTraceTokenPairApartDecisionV1
          ProofTraceApartPresentV1 ?left ?right
          ProofTracePairAllowedV1))
      (body))
    (rule proof-trace-token-pair-not-apart-v1
      (head
        (ProofTraceTokenPairApartDecisionV1
          ProofTraceApartMissingV1 ?left ?right
          (ProofTracePairConflictV1 ?left ?right)))
      (body))

    (rule proof-trace-token-against-sequence-nil-v1
      (head
        (ProofTraceTokenAgainstSequenceResultV1
          ?left ProofSequenceNilV1 ProofTracePairAllowedV1))
      (body))
    (rule proof-trace-token-against-sequence-cons-v1
      (head
        (ProofTraceTokenAgainstSequenceResultV1
          ?left (ProofSequenceConsV1 ?right ?rest) ?result))
      (body
        (ProofTraceTokenPairDecisionV1 ?left ?right ?head-result)
        (ProofTraceTokenAgainstSequenceHeadResultV1
          ?head-result ?left ?rest ?result)))
    (rule proof-trace-token-against-sequence-head-allowed-v1
      (head
        (ProofTraceTokenAgainstSequenceHeadResultV1
          ProofTracePairAllowedV1 ?left ?rest ?result))
      (body
        (ProofTraceTokenAgainstSequenceResultV1
          ?left ?rest ?result)))
    (rule proof-trace-token-against-sequence-head-conflict-v1
      (head
        (ProofTraceTokenAgainstSequenceHeadResultV1
          (ProofTracePairConflictV1 ?left ?right)
          ?token ?rest (ProofTracePairConflictV1 ?left ?right)))
      (body))

    (rule proof-trace-sequence-support-apart-nil-v1
      (head
        (ProofTraceSequenceSupportApartResultV1
          ProofSequenceNilV1 ?right ProofTracePairAllowedV1))
      (body))
    (rule proof-trace-sequence-support-apart-cons-v1
      (head
        (ProofTraceSequenceSupportApartResultV1
          (ProofSequenceConsV1 ?left ?rest) ?right ?result))
      (body
        (ProofTraceTokenAgainstSequenceResultV1
          ?left ?right ?head-result)
        (ProofTraceSequenceSupportApartHeadResultV1
          ?head-result ?rest ?right ?result)))
    (rule proof-trace-sequence-support-apart-head-allowed-v1
      (head
        (ProofTraceSequenceSupportApartHeadResultV1
          ProofTracePairAllowedV1 ?rest ?right ?result))
      (body
        (ProofTraceSequenceSupportApartResultV1
          ?rest ?right ?result)))
    (rule proof-trace-sequence-support-apart-head-conflict-v1
      (head
        (ProofTraceSequenceSupportApartHeadResultV1
          (ProofTracePairConflictV1 ?left ?right)
          ?rest ?sequence (ProofTracePairConflictV1 ?left ?right)))
      (body))

    (rule proof-trace-check-disjoints-nil-result-v1
      (head
        (ProofTraceCheckDisjointsResultV1
          ProofAssertionListNilV1 ?environment
          ProofTraceDisjointSucceededV1))
      (body))
    (rule proof-trace-check-disjoints-cons-result-v1
      (head
        (ProofTraceCheckDisjointsResultV1
          (ProofAssertionListConsV1
            (ProofAssertionDisjointV1 ?left ?right) ?rest)
          ?environment ?result))
      (body
        (ProofTokenDifferentV1 ?left ?right)
        (ProofEnvironmentLookupV1 ?environment ?left ?left-image)
        (ProofEnvironmentLookupV1 ?environment ?right ?right-image)
        (ProofTraceSequenceSupportApartResultV1
          ?left-image ?right-image ?head-result)
        (ProofTraceCheckDisjointHeadResultV1
          ?head-result ?left ?right ?rest ?environment ?result)))
    (rule proof-trace-check-disjoint-head-succeeded-v1
      (head
        (ProofTraceCheckDisjointHeadResultV1
          ProofTracePairAllowedV1 ?left ?right ?rest ?environment ?result))
      (body
        (ProofTraceCheckDisjointsResultV1
          ?rest ?environment ?result)))
    (rule proof-trace-check-disjoint-head-refused-v1
      (head
        (ProofTraceCheckDisjointHeadResultV1
          (ProofTracePairConflictV1 ?actual-left ?actual-right)
          ?required-left ?required-right ?rest ?environment
          (ProofTraceRefusedDisjointV1
            ?required-left ?required-right ?actual-left ?actual-right)))
      (body))
    (rule proof-trace-realize-essentials-refused-v1
      (head
        (ProofTraceRealizeEssentialsResultV1
          (ProofTraceRefusedEssentialMismatchV1
            ?template ?expected ?actual)
          ?conclusion-type ?conclusion-template
          ?disjoints ?environment ?rest
          (ProofTraceRealizeRefusedV1
            (ProofTraceRefusedEssentialMismatchV1
              ?template ?expected ?actual))))
      (body))
    (rule proof-trace-realize-assertion-v1
      (head
        (ProofTraceRealizeAssertionV1
          (ProofAssertionV1
            ?variables ?essentials ?conclusion-type
            ?conclusion-template ?disjoints)
          ?frame ?input ?output))
      (body
        (ProofTraceRealizeAssertionResultV1
          (ProofAssertionV1
            ?variables ?essentials ?conclusion-type
            ?conclusion-template ?disjoints)
          ?frame ?input (ProofTraceRealizeSucceededV1 ?output))))
    (rule proof-trace-apply-declared-assertion-result-v1
      (head
        (ProofTraceApplyAssertionResultV1
          (ProofAssertionV1
            ?variables ?essentials ?conclusion-type
            ?conclusion-template ?disjoints)
          ?frame ?input ?result))
      (body
        (ProofAssertionDeclaredV1
          (ProofAssertionV1
            ?variables ?essentials ?conclusion-type
            ?conclusion-template ?disjoints))
        (ProofTraceRealizeAssertionResultV1
          (ProofAssertionV1
            ?variables ?essentials ?conclusion-type
            ?conclusion-template ?disjoints)
          ?frame ?input ?result)))
    (rule proof-trace-apply-assertion-v1
      (head
        (ProofTraceApplyAssertionV1
          ?assertion ?frame ?input ?output))
      (body
        (ProofTraceApplyAssertionResultV1
          ?assertion ?frame ?input
          (ProofTraceRealizeSucceededV1 ?output))))

    (rule proof-trace-saved-append-nil-v1
      (head
        (ProofTraceSavedAppendV1
          ProofTraceSavedNilV1 ?formula
          (ProofTraceSavedConsV1 ?formula ProofTraceSavedNilV1)))
      (body))
    (rule proof-trace-saved-append-cons-v1
      (head
        (ProofTraceSavedAppendV1
          (ProofTraceSavedConsV1 ?head ?rest) ?formula
          (ProofTraceSavedConsV1 ?head ?output)))
      (body
        (ProofTraceSavedAppendV1 ?rest ?formula ?output)))
    (rule proof-trace-saved-lookup-zero-v1
      (head
        (ProofTraceSavedLookupV1
          ProofTraceNatZeroV1
          (ProofTraceSavedConsV1 ?formula ?rest) ?formula))
      (body))
    (rule proof-trace-saved-lookup-succ-v1
      (head
        (ProofTraceSavedLookupV1
          (ProofTraceNatSuccV1 ?index)
          (ProofTraceSavedConsV1 ?head ?rest) ?formula))
      (body
        (ProofTraceSavedLookupV1 ?index ?rest ?formula)))
    (rule proof-trace-saved-lookup-result-v1
      (head
        (ProofTraceSavedLookupResultV1
          ?index ?saved ?result))
      (body
        (ProofTraceSavedLookupFromV1
          ?index ?index ?saved ?result)))
    (rule proof-trace-saved-lookup-from-zero-found-v1
      (head
        (ProofTraceSavedLookupFromV1
          ?requested ProofTraceNatZeroV1
          (ProofTraceSavedConsV1 ?formula ?rest)
          (ProofTraceSavedFoundV1 ?formula)))
      (body))
    (rule proof-trace-saved-lookup-from-succ-v1
      (head
        (ProofTraceSavedLookupFromV1
          ?requested (ProofTraceNatSuccV1 ?index)
          (ProofTraceSavedConsV1 ?head ?rest) ?result))
      (body
        (ProofTraceSavedLookupFromV1
          ?requested ?index ?rest ?result)))
    (rule proof-trace-saved-lookup-from-missing-v1
      (head
        (ProofTraceSavedLookupFromV1
          ?requested ?index ProofTraceSavedNilV1
          (ProofTraceSavedMissingV1 ?requested)))
      (body))

    (rule proof-trace-exec-state-nil-v1
      (head
        (ProofTraceExecStateV1
          ?context ProofTraceNilV1 ?stack ?saved ?stack ?saved))
      (body))
    (rule proof-trace-exec-state-push-v1
      (head
        (ProofTraceExecStateV1
          ?context
          (ProofTraceConsV1
            (ProofTracePushV1 ?formula) ?trace)
          ?stack ?saved ?output ?output-saved))
      (body
        (ProofTraceContextMemberV1 ?context ?formula)
        (ProofTraceExecStateV1
          ?context ?trace
          (ProofTraceStackConsV1 ?formula ?stack) ?saved
          ?output ?output-saved)))
    (rule proof-trace-exec-state-apply-v1
      (head
        (ProofTraceExecStateV1
          ?context
          (ProofTraceConsV1
            (ProofTraceApplyV1 ?assertion ?frame) ?trace)
          ?stack ?saved ?output ?output-saved))
      (body
        (ProofTraceApplyAssertionV1
          ?assertion ?frame ?stack ?next-stack)
        (ProofTraceExecStateV1
          ?context ?trace ?next-stack ?saved ?output ?output-saved)))
    (rule proof-trace-exec-state-incomplete-v1
      (head
        (ProofTraceExecStateV1
          ?context
          (ProofTraceConsV1
            (ProofTraceIncompleteStepV1 ?formula) ?trace)
          ?stack ?saved ?output ?output-saved))
      (body
        (ProofTraceExecStateV1
          ?context ?trace
          (ProofTraceStackConsV1 ?formula ?stack) ?saved
          ?output ?output-saved)))
    (rule proof-trace-exec-state-save-v1
      (head
        (ProofTraceExecStateV1
          ?context
          (ProofTraceConsV1 ProofTraceSaveV1 ?trace)
          (ProofTraceStackConsV1 ?formula ?stack) ?saved
          ?output ?output-saved))
      (body
        (ProofTraceSavedAppendV1 ?saved ?formula ?next-saved)
        (ProofTraceExecStateV1
          ?context ?trace
          (ProofTraceStackConsV1 ?formula ?stack) ?next-saved
          ?output ?output-saved)))
    (rule proof-trace-exec-state-restore-v1
      (head
        (ProofTraceExecStateV1
          ?context
          (ProofTraceConsV1 (ProofTraceRestoreV1 ?index) ?trace)
          ?stack ?saved ?output ?output-saved))
      (body
        (ProofTraceSavedLookupV1 ?index ?saved ?formula)
        (ProofTraceExecStateV1
          ?context ?trace
          (ProofTraceStackConsV1 ?formula ?stack) ?saved
          ?output ?output-saved)))
    (rule proof-trace-exec-v1
      (head
        (ProofTraceExecV1 ?context ?trace ?input ?output))
      (body
        (ProofTraceExecStateV1
          ?context ?trace ?input ProofTraceSavedNilV1
          ?output ?output-saved)))

    ; A frame consumes one stack formula per mandatory hypothesis.  This
    ; decision is structural and total for admitted frame/stack constructors.
    (rule proof-trace-frame-stack-depth-frame-nil-v1
      (head
        (ProofTraceFrameStackDepthV1
          ProofTraceFrameNilV1 ?stack ProofTraceFrameStackFitsV1))
      (body))
    (rule proof-trace-frame-stack-depth-stack-nil-v1
      (head
        (ProofTraceFrameStackDepthV1
          (ProofTraceFrameConsV1 ?entry ?frame-rest)
          ProofTraceStackNilV1 ProofTraceFrameStackUnderflowV1))
      (body))
    (rule proof-trace-frame-stack-depth-cons-v1
      (head
        (ProofTraceFrameStackDepthV1
          (ProofTraceFrameConsV1 ?entry ?frame-rest)
          (ProofTraceStackConsV1 ?formula ?stack-rest) ?result))
      (body
        (ProofTraceFrameStackDepthV1
          ?frame-rest ?stack-rest ?result)))

    ; Result-producing chronological execution covers every success route plus
    ; explicit assertion-stack underflow and authored assertion refusals.  An
    ; apply instruction computes each structural decision once, then dispatches
    ; on its result constructor; it never searches by rerunning the assertion.
    (rule proof-trace-exec-state-result-nil-v1
      (head
        (ProofTraceExecStateResultV1
          ?context ProofTraceNilV1 ?stack ?saved
          (ProofTraceExecSucceededV1 ?stack ?saved)))
      (body))
    (rule proof-trace-exec-state-result-push-v1
      (head
        (ProofTraceExecStateResultV1
          ?context
          (ProofTraceConsV1 (ProofTracePushV1 ?formula) ?trace)
          ?stack ?saved ?result))
      (body
        (ProofTraceContextMemberV1 ?context ?formula)
        (ProofTraceExecStateResultV1
          ?context ?trace
          (ProofTraceStackConsV1 ?formula ?stack) ?saved ?result)))
    (rule proof-trace-exec-state-result-apply-v1
      (head
        (ProofTraceExecStateResultV1
          ?context
          (ProofTraceConsV1
            (ProofTraceApplyV1 ?assertion ?frame) ?trace)
          ?stack ?saved ?result))
      (body
        (ProofTraceFrameStackDepthV1 ?frame ?stack ?depth-result)
        (ProofTraceApplyDepthResultV1
          ?depth-result ?context ?trace ?assertion ?frame
          ?stack ?saved ?result)))
    (rule proof-trace-apply-depth-underflow-v1
      (head
        (ProofTraceApplyDepthResultV1
          ProofTraceFrameStackUnderflowV1
          ?context ?trace ?assertion ?frame ?stack ?saved
          (ProofTraceExecRefusedV1
            (ProofTraceRefusedStackUnderflowV1 ?frame ?stack)
            ?stack ?saved)))
      (body))
    (rule proof-trace-apply-depth-fits-v1
      (head
        (ProofTraceApplyDepthResultV1
          ProofTraceFrameStackFitsV1
          ?context ?trace ?assertion ?frame ?stack ?saved ?result))
      (body
        (ProofTraceApplyAssertionResultV1
          ?assertion ?frame ?stack ?application-result)
        (ProofTraceApplyExecutionResultV1
          ?application-result ?context ?trace ?stack ?saved ?result)))
    (rule proof-trace-apply-execution-succeeded-v1
      (head
        (ProofTraceApplyExecutionResultV1
          (ProofTraceRealizeSucceededV1 ?next-stack)
          ?context ?trace ?stack ?saved ?result))
      (body
        (ProofTraceExecStateResultV1
          ?context ?trace ?next-stack ?saved ?result)))
    (rule proof-trace-apply-execution-refused-v1
      (head
        (ProofTraceApplyExecutionResultV1
          (ProofTraceRealizeRefusedV1 ?reason)
          ?context ?trace ?stack ?saved
          (ProofTraceExecRefusedV1 ?reason ?stack ?saved)))
      (body))
    (rule proof-trace-exec-state-result-incomplete-v1
      (head
        (ProofTraceExecStateResultV1
          ?context
          (ProofTraceConsV1
            (ProofTraceIncompleteStepV1 ?formula) ?trace)
          ?stack ?saved ?result))
      (body
        (ProofTraceExecStateResultV1
          ?context ?trace
          (ProofTraceStackConsV1 ?formula ?stack) ?saved ?result)))
    (rule proof-trace-exec-state-result-save-v1
      (head
        (ProofTraceExecStateResultV1
          ?context
          (ProofTraceConsV1 ProofTraceSaveV1 ?trace)
          (ProofTraceStackConsV1 ?formula ?stack) ?saved ?result))
      (body
        (ProofTraceSavedAppendV1 ?saved ?formula ?next-saved)
        (ProofTraceExecStateResultV1
          ?context ?trace
          (ProofTraceStackConsV1 ?formula ?stack) ?next-saved ?result)))
    (rule proof-trace-exec-state-result-restore-v1
      (head
        (ProofTraceExecStateResultV1
          ?context
          (ProofTraceConsV1 (ProofTraceRestoreV1 ?index) ?trace)
          ?stack ?saved ?result))
      (body
        (ProofTraceSavedLookupResultV1 ?index ?saved ?lookup-result)
        (ProofTraceRestoreLookupExecutionResultV1
          ?lookup-result ?context ?trace ?stack ?saved ?result)))
    (rule proof-trace-restore-lookup-found-v1
      (head
        (ProofTraceRestoreLookupExecutionResultV1
          (ProofTraceSavedFoundV1 ?formula)
          ?context ?trace ?stack ?saved ?result))
      (body
        (ProofTraceExecStateResultV1
          ?context ?trace
          (ProofTraceStackConsV1 ?formula ?stack) ?saved ?result)))
    (rule proof-trace-restore-lookup-missing-v1
      (head
        (ProofTraceRestoreLookupExecutionResultV1
          (ProofTraceSavedMissingV1 ?index)
          ?context ?trace ?stack ?saved
          (ProofTraceExecRefusedV1
            (ProofTraceRefusedSavedStepOutOfRangeV1 ?index)
            ?stack ?saved)))
      (body))

    ; Finality is a total structural decision once chronological execution has
    ; produced a stack.  Formula comparison uses the source-owned token
    ; disequality relation, so backend equality never becomes proof semantics.
    (rule proof-sequence-compare-nil-nil-v1
      (head
        (ProofSequenceCompareV1
          ProofSequenceNilV1 ProofSequenceNilV1 ProofSequenceEqualV1))
      (body))
    (rule proof-sequence-compare-nil-cons-v1
      (head
        (ProofSequenceCompareV1
          ProofSequenceNilV1
          (ProofSequenceConsV1 ?right ?right-rest)
          ProofSequenceDifferentV1))
      (body))
    (rule proof-sequence-compare-cons-nil-v1
      (head
        (ProofSequenceCompareV1
          (ProofSequenceConsV1 ?left ?left-rest)
          ProofSequenceNilV1 ProofSequenceDifferentV1))
      (body))
    (rule proof-sequence-compare-cons-v1
      (head
        (ProofSequenceCompareV1
          (ProofSequenceConsV1 ?left ?left-rest)
          (ProofSequenceConsV1 ?right ?right-rest) ?result))
      (body
        (ProofTokenCompareV1 ?left ?right ?head-comparison)
        (ProofSequenceCompareHeadsV1
          ?head-comparison ?left-rest ?right-rest ?result)))
    (rule proof-sequence-compare-heads-equal-v1
      (head
        (ProofSequenceCompareHeadsV1
          ProofTokenEqualV1 ?left-rest ?right-rest ?result))
      (body
        (ProofSequenceCompareV1 ?left-rest ?right-rest ?result)))
    (rule proof-sequence-compare-heads-unequal-v1
      (head
        (ProofSequenceCompareHeadsV1
          ProofTokenUnequalV1 ?left-rest ?right-rest
          ProofSequenceDifferentV1))
      (body))

    (rule proof-trace-finalize-equal-v1
      (head
        (ProofTraceFinalizeCompareV1
          ProofSequenceEqualV1 ?target ?actual ProofTraceSucceededV1))
      (body))
    (rule proof-trace-finalize-different-v1
      (head
        (ProofTraceFinalizeCompareV1
          ProofSequenceDifferentV1 ?target ?actual
          (ProofTraceRefusedClaimMismatchV1 ?target ?actual)))
      (body))
    (rule proof-trace-final-empty-v1
      (head
        (ProofTraceFinalResultV1
          ProofTraceStackNilV1 ?target ProofTraceRefusedEmptyStackV1))
      (body))
    (rule proof-trace-final-single-v1
      (head
        (ProofTraceFinalResultV1
          (ProofTraceStackConsV1 ?actual ProofTraceStackNilV1)
          ?target ?result))
      (body
        (ProofSequenceCompareV1 ?target ?actual ?comparison)
        (ProofTraceFinalizeCompareV1
          ?comparison ?target ?actual ?result)))
    (rule proof-trace-final-extra-v1
      (head
        (ProofTraceFinalResultV1
          (ProofTraceStackConsV1
            ?top (ProofTraceStackConsV1 ?next ?rest))
          ?target
          (ProofTraceRefusedExtraStackV1
            (ProofTraceStackConsV1
              ?top (ProofTraceStackConsV1 ?next ?rest)))))
      (body))

    (rule proof-trace-check-result-v1
      (head
        (ProofTraceCheckResultV1 ?context ?trace ?target ?result))
      (body
        (ProofTraceExecStateResultV1
          ?context ?trace ProofTraceStackNilV1 ProofTraceSavedNilV1
          ?execution-result)
        (ProofTraceCheckExecutionResultV1
          ?execution-result ?target ?result)))
    (rule proof-trace-check-execution-result-succeeded-v1
      (head
        (ProofTraceCheckExecutionResultV1
          (ProofTraceExecSucceededV1 ?stack ?saved) ?target ?result))
      (body
        (ProofTraceFinalResultV1 ?stack ?target ?result)))
    (rule proof-trace-check-execution-result-refused-v1
      (head
        (ProofTraceCheckExecutionResultV1
          (ProofTraceExecRefusedV1 ?reason ?stack ?saved)
          ?target ?reason))
      (body))
    (rule proof-trace-result-is-refusal-empty-stack-v1
      (head
        (ProofTraceResultIsRefusalV1 ProofTraceRefusedEmptyStackV1))
      (body))
    (rule proof-trace-result-is-refusal-extra-stack-v1
      (head
        (ProofTraceResultIsRefusalV1
          (ProofTraceRefusedExtraStackV1 ?stack)))
      (body))
    (rule proof-trace-result-is-refusal-claim-mismatch-v1
      (head
        (ProofTraceResultIsRefusalV1
          (ProofTraceRefusedClaimMismatchV1 ?target ?actual)))
      (body))
    (rule proof-trace-result-is-refusal-disjoint-v1
      (head
        (ProofTraceResultIsRefusalV1
          (ProofTraceRefusedDisjointV1
            ?required-left ?required-right ?actual-left ?actual-right)))
      (body))
    (rule proof-trace-result-is-refusal-essential-mismatch-v1
      (head
        (ProofTraceResultIsRefusalV1
          (ProofTraceRefusedEssentialMismatchV1
            ?template ?expected ?actual)))
      (body))
    (rule proof-trace-result-is-refusal-explicit-mandatory-v1
      (head
        (ProofTraceResultIsRefusalV1
          (ProofTraceRefusedExplicitMandatoryHypothesisV1 ?label)))
      (body))
    (rule proof-trace-result-is-refusal-inactive-hypothesis-v1
      (head
        (ProofTraceResultIsRefusalV1
          (ProofTraceRefusedInactiveHypothesisV1 ?label)))
      (body))
    (rule proof-trace-result-is-refusal-unknown-label-v1
      (head
        (ProofTraceResultIsRefusalV1
          (ProofTraceRefusedUnknownLabelV1 ?label)))
      (body))
    (rule proof-trace-result-is-refusal-stack-underflow-v1
      (head
        (ProofTraceResultIsRefusalV1
          (ProofTraceRefusedStackUnderflowV1 ?frame ?stack)))
      (body))
    (rule proof-trace-result-is-refusal-saved-step-v1
      (head
        (ProofTraceResultIsRefusalV1
          (ProofTraceRefusedSavedStepOutOfRangeV1 ?index)))
      (body))
    (rule proof-trace-result-is-refusal-save-during-index-v1
      (head
        (ProofTraceResultIsRefusalV1
          (ProofTraceRefusedSaveDuringIndexV1 ?accumulator)))
      (body))
    (rule proof-trace-result-is-refusal-save-placement-v1
      (head
        (ProofTraceResultIsRefusalV1
          (ProofTraceRefusedSavePlacementV1 ?phase)))
      (body))
    (rule proof-trace-result-is-refusal-typecode-mismatch-v1
      (head
        (ProofTraceResultIsRefusalV1
          (ProofTraceRefusedTypecodeMismatchV1
            ?expected-typecode ?actual-typecode)))
      (body))
    (rule proof-trace-refusal-class-empty-stack-v1
      (head
        (ProofTraceRefusalClassV1
          ProofTraceRefusedEmptyStackV1 ProofTraceEmptyStackV1))
      (body))
    (rule proof-trace-refusal-class-extra-stack-v1
      (head
        (ProofTraceRefusalClassV1
          (ProofTraceRefusedExtraStackV1 ?stack) ProofTraceExtraStackV1))
      (body))
    (rule proof-trace-refusal-class-claim-mismatch-v1
      (head
        (ProofTraceRefusalClassV1
          (ProofTraceRefusedClaimMismatchV1 ?target ?actual)
          ProofTraceClaimMismatchV1))
      (body))
    (rule proof-trace-refusal-class-disjoint-v1
      (head
        (ProofTraceRefusalClassV1
          (ProofTraceRefusedDisjointV1
            ?required-left ?required-right ?actual-left ?actual-right)
          ProofTraceDisjointViolationV1))
      (body))
    (rule proof-trace-refusal-class-essential-mismatch-v1
      (head
        (ProofTraceRefusalClassV1
          (ProofTraceRefusedEssentialMismatchV1
            ?template ?expected ?actual)
          ProofTraceSubstitutionMismatchV1))
      (body))
    (rule proof-trace-refusal-class-explicit-mandatory-v1
      (head
        (ProofTraceRefusalClassV1
          (ProofTraceRefusedExplicitMandatoryHypothesisV1 ?label)
          ProofTraceExplicitMandatoryHypothesisV1))
      (body))
    (rule proof-trace-refusal-class-inactive-hypothesis-v1
      (head
        (ProofTraceRefusalClassV1
          (ProofTraceRefusedInactiveHypothesisV1 ?label)
          ProofTraceInactiveHypothesisV1))
      (body))
    (rule proof-trace-refusal-class-unknown-label-v1
      (head
        (ProofTraceRefusalClassV1
          (ProofTraceRefusedUnknownLabelV1 ?label)
          ProofTraceUnknownLabelV1))
      (body))
    (rule proof-trace-refusal-class-stack-underflow-v1
      (head
        (ProofTraceRefusalClassV1
          (ProofTraceRefusedStackUnderflowV1 ?frame ?stack)
          ProofTraceStackUnderflowV1))
      (body))
    (rule proof-trace-refusal-class-saved-step-v1
      (head
        (ProofTraceRefusalClassV1
          (ProofTraceRefusedSavedStepOutOfRangeV1 ?index)
          ProofTraceSavedStepOutOfRangeV1))
      (body))
    (rule proof-trace-refusal-class-save-during-index-v1
      (head
        (ProofTraceRefusalClassV1
          (ProofTraceRefusedSaveDuringIndexV1 ?accumulator)
          ProofTraceSaveDuringIndexV1))
      (body))
    (rule proof-trace-refusal-class-save-placement-v1
      (head
        (ProofTraceRefusalClassV1
          (ProofTraceRefusedSavePlacementV1 ?phase)
          ProofTraceSavePlacementV1))
      (body))
    (rule proof-trace-refusal-class-typecode-mismatch-v1
      (head
        (ProofTraceRefusalClassV1
          (ProofTraceRefusedTypecodeMismatchV1
            ?expected-typecode ?actual-typecode)
          ProofTraceTypecodeMismatchV1))
      (body))
    (rule proof-trace-accepted-v1
      (head
        (ProofTraceAcceptedV1 ?context ?trace ?target))
      (body
        (ProofTraceCheckResultV1
          ?context ?trace ?target ProofTraceSucceededV1)))
  ))
