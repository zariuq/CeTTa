; Finite compilation rules for SourceResolutionControlCoreV1.
; Payload-bearing source identities are specialized to symbolic control kinds;
; filesystem identity and bytes remain capabilities of the host resolver.

(gslt-presentation-v1 SourceResolutionControlCompilerV1
  (signature
    (operator SourceResolutionControlRecordV1 4)
    (operator SourcePolicyPairV1 2)
    (operator SourceResolveDecisionV1 5)
    (operator SourceResolutionOutcomeV1 3)
  )
  (equations)
  (rewrites
    (rule source-control-record-fresh-v1
      (head
        (SourceResolutionControlRecordV1
          SourceControlDecisionRecordV1 SourceProbeFreshKindV1
          (SourcePolicyPairV1 ?completedPolicy ?activePolicy)
          SourceDecisionExpandFreshV1))
      (body
        (SourceResolveDecisionV1
          SourceControlRequestV1
          (SourceProbeFreshV1 SourceControlIdV1)
          ?completedPolicy ?activePolicy
          (SourceExpandFreshV1 SourceControlIdV1))))
    (rule source-control-record-completed-skip-v1
      (head
        (SourceResolutionControlRecordV1
          SourceControlDecisionRecordV1 SourceProbeCompletedKindV1
          (SourcePolicyPairV1 SourceCompletedSkipV1 ?activePolicy)
          SourceDecisionSkipCompletedV1))
      (body
        (SourceResolveDecisionV1
          SourceControlRequestV1
          (SourceProbeCompletedV1 SourceControlIdV1)
          SourceCompletedSkipV1 ?activePolicy
          (SourceSkipCompletedV1 SourceControlIdV1))))
    (rule source-control-record-completed-reject-v1
      (head
        (SourceResolutionControlRecordV1
          SourceControlDecisionRecordV1 SourceProbeCompletedKindV1
          (SourcePolicyPairV1 SourceCompletedRejectV1 ?activePolicy)
          SourceDecisionRefuseCompletedV1))
      (body
        (SourceResolveDecisionV1
          SourceControlRequestV1
          (SourceProbeCompletedV1 SourceControlIdV1)
          SourceCompletedRejectV1 ?activePolicy
          (SourceRefuseV1
            (SourceRefusedCompletedV1 SourceControlIdV1)))))
    (rule source-control-record-active-skip-v1
      (head
        (SourceResolutionControlRecordV1
          SourceControlDecisionRecordV1 SourceProbeActiveKindV1
          (SourcePolicyPairV1 ?completedPolicy SourceActiveSkipV1)
          SourceDecisionSkipActiveV1))
      (body
        (SourceResolveDecisionV1
          SourceControlRequestV1
          (SourceProbeActiveV1 SourceControlIdV1)
          ?completedPolicy SourceActiveSkipV1
          (SourceSkipActiveV1 SourceControlIdV1))))
    (rule source-control-record-active-reject-v1
      (head
        (SourceResolutionControlRecordV1
          SourceControlDecisionRecordV1 SourceProbeActiveKindV1
          (SourcePolicyPairV1 ?completedPolicy SourceActiveRejectV1)
          SourceDecisionRefuseActiveV1))
      (body
        (SourceResolveDecisionV1
          SourceControlRequestV1
          (SourceProbeActiveV1 SourceControlIdV1)
          ?completedPolicy SourceActiveRejectV1
          (SourceRefuseV1
            (SourceRefusedActiveV1 SourceControlIdV1)))))
    (rule source-control-record-missing-v1
      (head
        (SourceResolutionControlRecordV1
          SourceControlDecisionRecordV1 SourceProbeMissingKindV1
          (SourcePolicyPairV1 ?completedPolicy ?activePolicy)
          SourceDecisionRefuseMissingV1))
      (body
        (SourceResolveDecisionV1
          SourceControlRequestV1 SourceProbeMissingV1
          ?completedPolicy ?activePolicy
          (SourceRefuseV1
            (SourceRefusedMissingV1 SourceControlRequestV1)))))
    (rule source-control-record-resource-v1
      (head
        (SourceResolutionControlRecordV1
          SourceControlDecisionRecordV1 SourceProbeResourceKindV1
          (SourcePolicyPairV1 ?completedPolicy ?activePolicy)
          SourceDecisionResourceV1))
      (body
        (SourceResolveDecisionV1
          SourceControlRequestV1 SourceProbeResourceV1
          ?completedPolicy ?activePolicy SourceResourceDecisionV1)))
    (rule source-control-record-invalid-v1
      (head
        (SourceResolutionControlRecordV1
          SourceControlDecisionRecordV1 SourceProbeInvalidKindV1
          (SourcePolicyPairV1 ?completedPolicy ?activePolicy)
          SourceDecisionInvalidV1))
      (body
        (SourceResolveDecisionV1
          SourceControlRequestV1 SourceProbeInvalidV1
          ?completedPolicy ?activePolicy SourceInvalidDecisionV1)))

    (rule source-control-record-finish-fresh-accepted-v1
      (head
        (SourceResolutionControlRecordV1
          SourceControlFinishRecordV1 SourceDecisionExpandFreshV1
          SourceChildAcceptedV1 SourceOutcomeAcceptedV1))
      (body
        (SourceResolutionOutcomeV1
          (SourceExpandFreshV1 SourceControlIdV1)
          SourceChildAcceptedV1
          (SourceAcceptedFreshV1 SourceControlIdV1))))
    (rule source-control-record-finish-fresh-refused-v1
      (head
        (SourceResolutionControlRecordV1
          SourceControlFinishRecordV1 SourceDecisionExpandFreshV1
          SourceChildRefusedV1 SourceOutcomeRefusedV1))
      (body
        (SourceResolutionOutcomeV1
          (SourceExpandFreshV1 SourceControlIdV1)
          SourceChildRefusedV1
          (SourceRefusedOutcomeV1
            (SourceRefusedChildV1
              SourceControlIdV1 SourceChildRefusedV1)))))
    (rule source-control-record-finish-fresh-resource-v1
      (head
        (SourceResolutionControlRecordV1
          SourceControlFinishRecordV1 SourceDecisionExpandFreshV1
          SourceChildResourceV1 SourceOutcomeResourceV1))
      (body
        (SourceResolutionOutcomeV1
          (SourceExpandFreshV1 SourceControlIdV1)
          SourceChildResourceV1 SourceResourceOutcomeV1)))
    (rule source-control-record-finish-fresh-invalid-v1
      (head
        (SourceResolutionControlRecordV1
          SourceControlFinishRecordV1 SourceDecisionExpandFreshV1
          SourceChildInvalidV1 SourceOutcomeInvalidV1))
      (body
        (SourceResolutionOutcomeV1
          (SourceExpandFreshV1 SourceControlIdV1)
          SourceChildInvalidV1 SourceInvalidOutcomeV1)))
    (rule source-control-record-finish-completed-skip-v1
      (head
        (SourceResolutionControlRecordV1
          SourceControlFinishRecordV1 SourceDecisionSkipCompletedV1
          SourceNoChildV1 SourceOutcomeAcceptedV1))
      (body
        (SourceResolutionOutcomeV1
          (SourceSkipCompletedV1 SourceControlIdV1)
          SourceNoChildV1
          (SourceAcceptedCompletedSkipV1 SourceControlIdV1))))
    (rule source-control-record-finish-active-skip-v1
      (head
        (SourceResolutionControlRecordV1
          SourceControlFinishRecordV1 SourceDecisionSkipActiveV1
          SourceNoChildV1 SourceOutcomeAcceptedV1))
      (body
        (SourceResolutionOutcomeV1
          (SourceSkipActiveV1 SourceControlIdV1)
          SourceNoChildV1
          (SourceAcceptedActiveSkipV1 SourceControlIdV1))))
    (rule source-control-record-finish-completed-refused-v1
      (head
        (SourceResolutionControlRecordV1
          SourceControlFinishRecordV1 SourceDecisionRefuseCompletedV1
          SourceNoChildV1 SourceOutcomeRefusedV1))
      (body
        (SourceResolutionOutcomeV1
          (SourceRefuseV1
            (SourceRefusedCompletedV1 SourceControlIdV1))
          SourceNoChildV1
          (SourceRefusedOutcomeV1
            (SourceRefusedCompletedV1 SourceControlIdV1)))))
    (rule source-control-record-finish-active-refused-v1
      (head
        (SourceResolutionControlRecordV1
          SourceControlFinishRecordV1 SourceDecisionRefuseActiveV1
          SourceNoChildV1 SourceOutcomeRefusedV1))
      (body
        (SourceResolutionOutcomeV1
          (SourceRefuseV1
            (SourceRefusedActiveV1 SourceControlIdV1))
          SourceNoChildV1
          (SourceRefusedOutcomeV1
            (SourceRefusedActiveV1 SourceControlIdV1)))))
    (rule source-control-record-finish-missing-refused-v1
      (head
        (SourceResolutionControlRecordV1
          SourceControlFinishRecordV1 SourceDecisionRefuseMissingV1
          SourceNoChildV1 SourceOutcomeRefusedV1))
      (body
        (SourceResolutionOutcomeV1
          (SourceRefuseV1
            (SourceRefusedMissingV1 SourceControlRequestV1))
          SourceNoChildV1
          (SourceRefusedOutcomeV1
            (SourceRefusedMissingV1 SourceControlRequestV1)))))
    (rule source-control-record-finish-resource-v1
      (head
        (SourceResolutionControlRecordV1
          SourceControlFinishRecordV1 SourceDecisionResourceV1
          SourceNoChildV1 SourceOutcomeResourceV1))
      (body
        (SourceResolutionOutcomeV1
          SourceResourceDecisionV1 SourceNoChildV1
          SourceResourceOutcomeV1)))
    (rule source-control-record-finish-invalid-v1
      (head
        (SourceResolutionControlRecordV1
          SourceControlFinishRecordV1 SourceDecisionInvalidV1
          SourceNoChildV1 SourceOutcomeInvalidV1))
      (body
        (SourceResolutionOutcomeV1
          SourceInvalidDecisionV1 SourceNoChildV1
          SourceInvalidOutcomeV1)))
  ))
