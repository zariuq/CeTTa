!(gslt-rho:query:ProofTraceCompileNormalStatusV1
   QueryMetamathProofTraceCompileNormalNilV1
   (ProofTraceCompileNormalStatusV1
     ProofTraceLabelNilV1 ProofTraceNoTargetV1
     ProofTraceStatusCompleteV1 $trace))

!(gslt-rho:query:ProofTraceCompileNormalStatusV1
   QueryMetamathProofTraceCompileNormalOneV1
   (ProofTraceCompileNormalStatusV1
     (ProofTraceLabelConsV1 input-hx-v1 ProofTraceLabelNilV1)
     ProofTraceNoTargetV1 ProofTraceStatusCompleteV1 $trace))

!(gslt-rho:query:ProofTraceCompileNormalStatusV1
   QueryMetamathProofTraceCompileNormalApplyOneV1
   (ProofTraceCompileNormalStatusV1
     (ProofTraceLabelConsV1 input-identity-v1 ProofTraceLabelNilV1)
     ProofTraceNoTargetV1 ProofTraceStatusCompleteV1 $trace))

!(gslt-rho:query:ProofTraceCompileNormalStatusV1
   QueryMetamathProofTraceCompileNormalTwoV1
   (ProofTraceCompileNormalStatusV1
     (ProofTraceLabelConsV1 input-hx-v1
       (ProofTraceLabelConsV1 input-identity-v1 ProofTraceLabelNilV1))
     ProofTraceNoTargetV1 ProofTraceStatusCompleteV1 $trace))

!(gslt-rho:query:ProofTraceCompileNormalStatusV1
   QueryMetamathProofTraceCompileNormalTwoPushesV1
   (ProofTraceCompileNormalStatusV1
     (ProofTraceLabelConsV1 input-hx-v1
       (ProofTraceLabelConsV1 input-hx-v1 ProofTraceLabelNilV1))
     ProofTraceNoTargetV1 ProofTraceStatusCompleteV1 $trace))

!(gslt-rho:query:ProofTraceCompileNormalStatusV1
   QueryMetamathProofTraceCompileNormalReverseV1
   (ProofTraceCompileNormalStatusV1
     (ProofTraceLabelConsV1 input-identity-v1
       (ProofTraceLabelConsV1 input-hx-v1 ProofTraceLabelNilV1))
     ProofTraceNoTargetV1 ProofTraceStatusCompleteV1 $trace))
