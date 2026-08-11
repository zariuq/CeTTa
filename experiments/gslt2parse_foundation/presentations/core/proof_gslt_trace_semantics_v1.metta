; Generic chronological execution for sequence-proof traces.  The ordered
; frame is consumed from its tail because the last mandatory hypothesis is
; the topmost stack entry.  Popped entries already carry trace provenance;
; this layer checks their shapes, substitution, essential instantiation, and
; apartness obligations without re-deriving them as ambient theorems.

(gslt-presentation-v1 ProofGSLTTraceSemanticsV1
  (signature
    (operator ProofTraceAcceptedV1 3)
    (operator ProofTraceApplyAssertionV1 4)
    (operator ProofTraceApplyV1 2)
    (operator ProofTraceBuildEnvironmentV1 3)
    (operator ProofTraceCheckEssentialsV1 3)
    (operator ProofTraceConsV1 2)
    (operator ProofTraceContextConsV1 2)
    (operator ProofTraceContextMemberV1 2)
    (operator ProofTraceExecV1 4)
    (operator ProofTraceExecStateV1 6)
    (operator ProofTraceFrameApplyV1 5)
    (operator ProofTraceFrameConsV1 2)
    (operator ProofTraceFrameEssentialV1 1)
    (operator ProofTraceFrameProjectV1 3)
    (operator ProofTraceFrameVariableV1 2)
    (operator ProofTraceIncompleteStepV1 1)
    (operator ProofTraceNatSuccV1 1)
    (operator ProofTracePushV1 1)
    (operator ProofTraceRestoreV1 1)
    (operator ProofTraceSavedAppendV1 3)
    (operator ProofTraceSavedConsV1 2)
    (operator ProofTraceSavedLookupV1 3)
    (operator ProofTraceStackConsV1 2)
  )
  (equations)
  (rewrites
    (rule proof-trace-context-member-head-v1
      (head
        (ProofTraceContextMemberV1
          (ProofTraceContextConsV1 ?formula ?rest) ?formula))
      (body))
    (rule proof-trace-context-member-tail-v1
      (head
        (ProofTraceContextMemberV1
          (ProofTraceContextConsV1 ?head ?rest) ?formula))
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

    (rule proof-trace-apply-assertion-v1
      (head
        (ProofTraceApplyAssertionV1
          (ProofAssertionV1
            ?variables ?essentials ?conclusion-type
            ?conclusion-template ?disjoints)
          ?frame ?input
          (ProofTraceStackConsV1
            (ProofSequenceConsV1 ?conclusion-type ?result) ?rest)))
      (body
        (ProofAssertionDeclaredV1
          (ProofAssertionV1
            ?variables ?essentials ?conclusion-type
            ?conclusion-template ?disjoints))
        (ProofTraceFrameProjectV1 ?frame ?variables ?essentials)
        (ProofTraceFrameApplyV1
          ?frame ?input ?images ?actuals ?rest)
        (ProofTraceBuildEnvironmentV1
          ?variables ?images ?environment)
        (ProofTraceCheckEssentialsV1
          ?essentials ?actuals ?environment)
        (ProofAssertionCheckDisjointsV1 ?disjoints ?environment)
        (ProofSequenceInstantiateV1
          ?conclusion-template ?environment ?result)))

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
    (rule proof-trace-accepted-v1
      (head
        (ProofTraceAcceptedV1 ?context ?trace ?target))
      (body
        (ProofTraceExecV1
          ?context ?trace ProofTraceStackNilV1
          (ProofTraceStackConsV1 ?target ProofTraceStackNilV1))))
  ))
