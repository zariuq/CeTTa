; Test-only observers that retain the normalized trace while joining the
; chronological input adapter, trace compiler, and trace executor.

(gslt-presentation-v1 ProofGSLTTraceEvidenceCanaryV1
  (signature
    (operator ProofTraceCanaryCompressedAcceptedV1 1)
    (operator ProofTraceCanaryCompressedEvidenceAcceptedV1 1)
    (operator ProofTraceCanaryEssentialsRefusedV1 1)
    (operator ProofTraceCanaryEssentialsSucceededV1 1)
    (operator ProofTraceCanaryNormalAcceptedV1 1)
    (operator ProofTraceCanaryNormalEvidenceAcceptedV1 1)
    (operator ProofTraceCanaryWrongTargetAcceptedV1 1)
    (operator ProofTraceInputCompressedEvidenceV1 6)
    (operator ProofTraceInputNormalEvidenceV1 4)
  )
  (equations)
  (rewrites
    (rule proof-trace-input-normal-evidence-v1
      (head
        (ProofTraceInputNormalEvidenceV1
          ?request ?labels ?target ?trace))
      (body
        (source-proof-trace-input-request-v1 ?request ?target)
        (source-proof-trace-input-context-count-v1
          ?request ?context-count)
        (ProofTraceInputBuildContextV1
          ?request ProofTraceNatZeroV1 ?context-count ?context)
        (ProofTraceCompileNormalV1 ?labels ?trace)
        (ProofTraceAcceptedV1 ?context ?trace ?target)))

    (rule proof-trace-input-compressed-evidence-v1
      (head
        (ProofTraceInputCompressedEvidenceV1
          ?request ?initial-labels ?explicit-labels ?codes ?target ?trace))
      (body
        (source-proof-trace-input-request-v1 ?request ?target)
        (source-proof-trace-input-context-count-v1
          ?request ?context-count)
        (ProofTraceInputBuildContextV1
          ?request ProofTraceNatZeroV1 ?context-count ?context)
        (ProofTraceCompileCompressedV1
          ?initial-labels ?explicit-labels ?codes ?trace)
        (ProofTraceAcceptedV1 ?context ?trace ?target)))

    ; Ground observation rules are part of the source composition so lowering
    ; can select their deterministic binding modes.  The generic relational
    ; projections remain available for open queries.
    (rule proof-trace-canary-normal-accepted-v1
      (head (ProofTraceCanaryNormalAcceptedV1 AcceptedV1))
      (body
        (ProofTraceCanaryNormalEvidenceAcceptedV1 AcceptedV1)))

    (rule proof-trace-canary-compressed-accepted-v1
      (head (ProofTraceCanaryCompressedAcceptedV1 AcceptedV1))
      (body
        (ProofTraceCanaryCompressedEvidenceAcceptedV1 AcceptedV1)))

    (rule proof-trace-canary-normal-evidence-accepted-v1
      (head (ProofTraceCanaryNormalEvidenceAcceptedV1 AcceptedV1))
      (body
        (ProofTraceInputNormalEvidenceV1
          input-request-v1
          (ProofTraceLabelConsV1 input-hx-v1
            (ProofTraceLabelConsV1
              input-identity-v1 ProofTraceLabelNilV1))
          (ProofSequenceConsV1 input-wff-v1
            (ProofSequenceConsV1 input-a-v1 ProofSequenceNilV1))
          (ProofTraceConsV1
            (ProofTracePushV1
              (ProofSequenceConsV1 input-wff-v1
                (ProofSequenceConsV1 input-a-v1 ProofSequenceNilV1)))
            (ProofTraceConsV1
              (ProofTraceApplyV1
                (ProofAssertionV1
                  (ProofAssertionListConsV1
                    (ProofAssertionVariableV1 input-x-v1 input-wff-v1)
                    ProofAssertionListNilV1)
                  ProofAssertionListNilV1
                  input-wff-v1
                  (ProofSequenceConsV1 input-x-v1 ProofSequenceNilV1)
                  ProofAssertionListNilV1)
                (ProofTraceFrameConsV1
                  (ProofTraceFrameVariableV1 input-x-v1 input-wff-v1)
                  ProofTraceFrameNilV1))
              ProofTraceNilV1)))))

    (rule proof-trace-canary-compressed-evidence-accepted-v1
      (head (ProofTraceCanaryCompressedEvidenceAcceptedV1 AcceptedV1))
      (body
        (ProofTraceInputCompressedEvidenceV1
          input-request-v1
          (ProofTraceLabelConsV1 input-hx-v1 ProofTraceLabelNilV1)
          (ProofTraceLabelConsV1 input-identity-v1 ProofTraceLabelNilV1)
          (ProofTraceCodeConsV1 65
            (ProofTraceCodeConsV1 66 ProofTraceCodeNilV1))
          (ProofSequenceConsV1 input-wff-v1
            (ProofSequenceConsV1 input-a-v1 ProofSequenceNilV1))
          (ProofTraceConsV1
            (ProofTracePushV1
              (ProofSequenceConsV1 input-wff-v1
                (ProofSequenceConsV1 input-a-v1 ProofSequenceNilV1)))
            (ProofTraceConsV1
              (ProofTraceApplyV1
                (ProofAssertionV1
                  (ProofAssertionListConsV1
                    (ProofAssertionVariableV1 input-x-v1 input-wff-v1)
                    ProofAssertionListNilV1)
                  ProofAssertionListNilV1
                  input-wff-v1
                  (ProofSequenceConsV1 input-x-v1 ProofSequenceNilV1)
                  ProofAssertionListNilV1)
                (ProofTraceFrameConsV1
                  (ProofTraceFrameVariableV1 input-x-v1 input-wff-v1)
                  ProofTraceFrameNilV1))
              ProofTraceNilV1)))))

    (rule proof-trace-canary-wrong-target-accepted-v1
      (head (ProofTraceCanaryWrongTargetAcceptedV1 UnexpectedV1))
      (body
        (ProofTraceInputNormalEvidenceV1
          input-request-v1
          (ProofTraceLabelConsV1
            input-hdummy-v1 ProofTraceLabelNilV1)
          (ProofSequenceConsV1 input-wff-v1
            (ProofSequenceConsV1 input-a-v1 ProofSequenceNilV1))
          (ProofTraceConsV1
            (ProofTracePushV1
              (ProofSequenceConsV1 input-wff-v1
                (ProofSequenceConsV1
                  input-dummy-v1 ProofSequenceNilV1)))
            ProofTraceNilV1))))

    (rule proof-trace-canary-essentials-succeeded-v1
      (head
        (ProofTraceCanaryEssentialsSucceededV1
          ProofTraceEssentialsSucceededV1))
      (body
        (ProofTraceCheckEssentialsResultV1
          (ProofAssertionListConsV1
            (ProofAssertionEssentialV1
              (ProofSequenceConsV1 input-x-v1 ProofSequenceNilV1))
            ProofAssertionListNilV1)
          (ProofAssertionListConsV1
            (ProofSequenceConsV1 input-a-v1 ProofSequenceNilV1)
            ProofAssertionListNilV1)
          (ProofEnvironmentConsV1
            input-x-v1
            (ProofSequenceConsV1 input-a-v1 ProofSequenceNilV1)
            ProofEnvironmentNilV1)
          ProofTraceEssentialsSucceededV1)))

    (rule proof-trace-canary-essentials-refused-v1
      (head
        (ProofTraceCanaryEssentialsRefusedV1
          (ProofTraceRefusedEssentialMismatchV1
            (ProofSequenceConsV1 input-x-v1 ProofSequenceNilV1)
            (ProofSequenceConsV1 input-a-v1 ProofSequenceNilV1)
            (ProofSequenceConsV1 input-dummy-v1 ProofSequenceNilV1))))
      (body
        (ProofTraceCheckEssentialsResultV1
          (ProofAssertionListConsV1
            (ProofAssertionEssentialV1
              (ProofSequenceConsV1 input-x-v1 ProofSequenceNilV1))
            ProofAssertionListNilV1)
          (ProofAssertionListConsV1
            (ProofSequenceConsV1 input-dummy-v1 ProofSequenceNilV1)
            ProofAssertionListNilV1)
          (ProofEnvironmentConsV1
            input-x-v1
            (ProofSequenceConsV1 input-a-v1 ProofSequenceNilV1)
            ProofEnvironmentNilV1)
          (ProofTraceRefusedEssentialMismatchV1
            (ProofSequenceConsV1 input-x-v1 ProofSequenceNilV1)
            (ProofSequenceConsV1 input-a-v1 ProofSequenceNilV1)
            (ProofSequenceConsV1 input-dummy-v1 ProofSequenceNilV1)))))
  ))
