!(collapse
   (let $_proof
     (horn:prove-v1
       (ProofTraceInputVerifyNormalV1
         input-request-v1
         (ProofTraceLabelConsV1 input-hx-v1
           (ProofTraceLabelConsV1
             input-identity-v1 ProofTraceLabelNilV1))))
     (MetamathProofTracePeTTaNormalV1 AcceptedV1)))

!(collapse
   (let $_proof
     (horn:prove-v1
       (ProofTraceInputVerifyCompressedV1
         input-request-v1
         (ProofTraceLabelConsV1 input-hx-v1 ProofTraceLabelNilV1)
         (ProofTraceLabelConsV1 input-identity-v1 ProofTraceLabelNilV1)
         (ProofTraceCodeConsV1 65
           (ProofTraceCodeConsV1 66 ProofTraceCodeNilV1))))
     (MetamathProofTracePeTTaCompressedV1 AcceptedV1)))

!(collapse
   (let $_proof
     (horn:prove-v1
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
             ProofTraceNilV1))))
     (MetamathProofTracePeTTaNormalEvidenceV1
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

!(collapse
   (let $_proof
     (horn:prove-v1
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
             ProofTraceNilV1))))
     (MetamathProofTracePeTTaCompressedEvidenceV1
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

!(collapse
   (let $_proof
     (horn:prove-v1
       (ProofTraceInputVerifyNormalV1
         input-request-v1
         (ProofTraceLabelConsV1
           input-hdummy-v1 ProofTraceLabelNilV1)))
     (MetamathProofTracePeTTaWrongTargetV1 UnexpectedV1)))
