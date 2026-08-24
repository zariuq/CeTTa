; Language-neutral control semantics for recursively composed source files.
; A host capability reports source identity and availability.  This GSLT owns
; the language decisions: expand, skip, refuse, propagate a resource bound, or
; reject an invalid capability result.  File-system access is not a rewrite.

(gslt-presentation-v1 SourceResolutionControlCoreV1
  (signature
    (operator SourceActivePolicyV1 1)
    (operator SourceCompletedPolicyV1 1)
    (operator SourceResolveDecisionV1 5)
    (operator SourceResolutionOutcomeV1 3)

    (operator SourceProbeFreshV1 1)
    (operator SourceProbeCompletedV1 1)
    (operator SourceProbeActiveV1 1)
    (operator SourceExpandFreshV1 1)
    (operator SourceSkipCompletedV1 1)
    (operator SourceSkipActiveV1 1)
    (operator SourceRefuseV1 1)

    (operator SourceAcceptedFreshV1 1)
    (operator SourceAcceptedCompletedSkipV1 1)
    (operator SourceAcceptedActiveSkipV1 1)
    (operator SourceRefusedOutcomeV1 1)

    (operator SourceRefusedCompletedV1 1)
    (operator SourceRefusedActiveV1 1)
    (operator SourceRefusedMissingV1 1)
    (operator SourceRefusedChildV1 2)
  )
  (equations)
  (rewrites
    (rule source-completed-policy-skip-v1
      (head (SourceCompletedPolicyV1 SourceCompletedSkipV1))
      (body))
    (rule source-completed-policy-reject-v1
      (head (SourceCompletedPolicyV1 SourceCompletedRejectV1))
      (body))
    (rule source-active-policy-skip-v1
      (head (SourceActivePolicyV1 SourceActiveSkipV1))
      (body))
    (rule source-active-policy-reject-v1
      (head (SourceActivePolicyV1 SourceActiveRejectV1))
      (body))

    (rule source-resolve-fresh-v1
      (head
        (SourceResolveDecisionV1
          ?request (SourceProbeFreshV1 ?sourceId)
          ?completedPolicy ?activePolicy
          (SourceExpandFreshV1 ?sourceId)))
      (body
        (SourceCompletedPolicyV1 ?completedPolicy)
        (SourceActivePolicyV1 ?activePolicy)))
    (rule source-resolve-completed-skip-v1
      (head
        (SourceResolveDecisionV1
          ?request (SourceProbeCompletedV1 ?sourceId)
          SourceCompletedSkipV1 ?activePolicy
          (SourceSkipCompletedV1 ?sourceId)))
      (body (SourceActivePolicyV1 ?activePolicy)))
    (rule source-resolve-completed-reject-v1
      (head
        (SourceResolveDecisionV1
          ?request (SourceProbeCompletedV1 ?sourceId)
          SourceCompletedRejectV1 ?activePolicy
          (SourceRefuseV1
            (SourceRefusedCompletedV1 ?sourceId))))
      (body (SourceActivePolicyV1 ?activePolicy)))
    (rule source-resolve-active-skip-v1
      (head
        (SourceResolveDecisionV1
          ?request (SourceProbeActiveV1 ?sourceId)
          ?completedPolicy SourceActiveSkipV1
          (SourceSkipActiveV1 ?sourceId)))
      (body (SourceCompletedPolicyV1 ?completedPolicy)))
    (rule source-resolve-active-reject-v1
      (head
        (SourceResolveDecisionV1
          ?request (SourceProbeActiveV1 ?sourceId)
          ?completedPolicy SourceActiveRejectV1
          (SourceRefuseV1
            (SourceRefusedActiveV1 ?sourceId))))
      (body (SourceCompletedPolicyV1 ?completedPolicy)))
    (rule source-resolve-missing-v1
      (head
        (SourceResolveDecisionV1
          ?request SourceProbeMissingV1
          ?completedPolicy ?activePolicy
          (SourceRefuseV1 (SourceRefusedMissingV1 ?request))))
      (body
        (SourceCompletedPolicyV1 ?completedPolicy)
        (SourceActivePolicyV1 ?activePolicy)))
    (rule source-resolve-resource-v1
      (head
        (SourceResolveDecisionV1
          ?request SourceProbeResourceV1
          ?completedPolicy ?activePolicy
          SourceResourceDecisionV1))
      (body
        (SourceCompletedPolicyV1 ?completedPolicy)
        (SourceActivePolicyV1 ?activePolicy)))
    (rule source-resolve-invalid-v1
      (head
        (SourceResolveDecisionV1
          ?request SourceProbeInvalidV1
          ?completedPolicy ?activePolicy
          SourceInvalidDecisionV1))
      (body
        (SourceCompletedPolicyV1 ?completedPolicy)
        (SourceActivePolicyV1 ?activePolicy)))

    (rule source-finish-fresh-accepted-v1
      (head
        (SourceResolutionOutcomeV1
          (SourceExpandFreshV1 ?sourceId)
          SourceChildAcceptedV1
          (SourceAcceptedFreshV1 ?sourceId)))
      (body))
    (rule source-finish-fresh-refused-v1
      (head
        (SourceResolutionOutcomeV1
          (SourceExpandFreshV1 ?sourceId)
          SourceChildRefusedV1
          (SourceRefusedOutcomeV1
            (SourceRefusedChildV1 ?sourceId SourceChildRefusedV1))))
      (body))
    (rule source-finish-fresh-resource-v1
      (head
        (SourceResolutionOutcomeV1
          (SourceExpandFreshV1 ?sourceId)
          SourceChildResourceV1
          SourceResourceOutcomeV1))
      (body))
    (rule source-finish-fresh-invalid-v1
      (head
        (SourceResolutionOutcomeV1
          (SourceExpandFreshV1 ?sourceId)
          SourceChildInvalidV1
          SourceInvalidOutcomeV1))
      (body))
    (rule source-finish-completed-skip-v1
      (head
        (SourceResolutionOutcomeV1
          (SourceSkipCompletedV1 ?sourceId)
          SourceNoChildV1
          (SourceAcceptedCompletedSkipV1 ?sourceId)))
      (body))
    (rule source-finish-active-skip-v1
      (head
        (SourceResolutionOutcomeV1
          (SourceSkipActiveV1 ?sourceId)
          SourceNoChildV1
          (SourceAcceptedActiveSkipV1 ?sourceId)))
      (body))
    (rule source-finish-refused-v1
      (head
        (SourceResolutionOutcomeV1
          (SourceRefuseV1 ?reason)
          SourceNoChildV1
          (SourceRefusedOutcomeV1 ?reason)))
      (body))
    (rule source-finish-resource-v1
      (head
        (SourceResolutionOutcomeV1
          SourceResourceDecisionV1
          SourceNoChildV1
          SourceResourceOutcomeV1))
      (body))
    (rule source-finish-invalid-v1
      (head
        (SourceResolutionOutcomeV1
          SourceInvalidDecisionV1
          SourceNoChildV1
          SourceInvalidOutcomeV1))
      (body))
  ))
