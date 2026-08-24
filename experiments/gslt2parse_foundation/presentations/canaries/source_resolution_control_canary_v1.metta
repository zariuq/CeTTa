; Positive and negative observations for the source-resolution control GSLT.

(gslt-presentation-v1 SourceResolutionControlCanaryV1
  (signature
    (operator SourceResolutionScenarioV1 6)
    (operator SourceResolutionObservationV1 3)
    (operator SourceResolveDecisionV1 5)
    (operator SourceResolutionOutcomeV1 3)
  )
  (equations)
  (rewrites
    (rule source-resolution-observation-v1
      (head
        (SourceResolutionObservationV1 ?name ?decision ?outcome))
      (body
        (SourceResolutionScenarioV1
          ?name ?request ?probe ?completedPolicy ?activePolicy ?child)
        (SourceResolveDecisionV1
          ?request ?probe ?completedPolicy ?activePolicy ?decision)
        (SourceResolutionOutcomeV1 ?decision ?child ?outcome)))

    (rule source-scenario-fresh-accepted-v1
      (head
        (SourceResolutionScenarioV1
          FreshAcceptedV1 SourcePathA (SourceProbeFreshV1 SourceIdA)
          SourceCompletedSkipV1 SourceActiveRejectV1
          SourceChildAcceptedV1))
      (body))
    (rule source-scenario-fresh-refused-v1
      (head
        (SourceResolutionScenarioV1
          FreshRefusedV1 SourcePathA (SourceProbeFreshV1 SourceIdA)
          SourceCompletedSkipV1 SourceActiveRejectV1
          SourceChildRefusedV1))
      (body))
    (rule source-scenario-fresh-resource-v1
      (head
        (SourceResolutionScenarioV1
          FreshResourceV1 SourcePathA (SourceProbeFreshV1 SourceIdA)
          SourceCompletedSkipV1 SourceActiveRejectV1
          SourceChildResourceV1))
      (body))
    (rule source-scenario-fresh-invalid-v1
      (head
        (SourceResolutionScenarioV1
          FreshInvalidV1 SourcePathA (SourceProbeFreshV1 SourceIdA)
          SourceCompletedSkipV1 SourceActiveRejectV1
          SourceChildInvalidV1))
      (body))
    (rule source-scenario-completed-skip-v1
      (head
        (SourceResolutionScenarioV1
          CompletedSkipV1 SourcePathA (SourceProbeCompletedV1 SourceIdA)
          SourceCompletedSkipV1 SourceActiveRejectV1 SourceNoChildV1))
      (body))
    (rule source-scenario-completed-reject-v1
      (head
        (SourceResolutionScenarioV1
          CompletedRejectV1 SourcePathA (SourceProbeCompletedV1 SourceIdA)
          SourceCompletedRejectV1 SourceActiveRejectV1 SourceNoChildV1))
      (body))
    (rule source-scenario-active-skip-v1
      (head
        (SourceResolutionScenarioV1
          ActiveSkipV1 SourcePathA (SourceProbeActiveV1 SourceIdA)
          SourceCompletedSkipV1 SourceActiveSkipV1 SourceNoChildV1))
      (body))
    (rule source-scenario-active-reject-v1
      (head
        (SourceResolutionScenarioV1
          ActiveRejectV1 SourcePathA (SourceProbeActiveV1 SourceIdA)
          SourceCompletedSkipV1 SourceActiveRejectV1 SourceNoChildV1))
      (body))
    (rule source-scenario-missing-v1
      (head
        (SourceResolutionScenarioV1
          MissingV1 SourcePathA SourceProbeMissingV1
          SourceCompletedSkipV1 SourceActiveRejectV1 SourceNoChildV1))
      (body))
    (rule source-scenario-resource-v1
      (head
        (SourceResolutionScenarioV1
          ResourceV1 SourcePathA SourceProbeResourceV1
          SourceCompletedSkipV1 SourceActiveRejectV1 SourceNoChildV1))
      (body))
    (rule source-scenario-invalid-v1
      (head
        (SourceResolutionScenarioV1
          InvalidV1 SourcePathA SourceProbeInvalidV1
          SourceCompletedSkipV1 SourceActiveRejectV1 SourceNoChildV1))
      (body))
    (rule source-scenario-invalid-policy-v1
      (head
        (SourceResolutionScenarioV1
          InvalidPolicyV1 SourcePathA (SourceProbeFreshV1 SourceIdA)
          SourceCompletedUnknownV1 SourceActiveRejectV1
          SourceChildAcceptedV1))
      (body))
  ))
