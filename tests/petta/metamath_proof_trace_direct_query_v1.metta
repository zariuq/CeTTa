!(collapse
   (let $_proof
     (gslt:ProofTraceCanaryNormalAcceptedV1
       (ProofTraceCanaryNormalAcceptedV1 AcceptedV1))
     (MetamathProofTraceDirectNormalV1 AcceptedV1)))

!(collapse
   (let $_proof
     (gslt:ProofTraceCanaryCompressedAcceptedV1
       (ProofTraceCanaryCompressedAcceptedV1 AcceptedV1))
     (MetamathProofTraceDirectCompressedV1 AcceptedV1)))

!(collapse
   (let $_proof
     (gslt:ProofTraceCanaryNormalEvidenceAcceptedV1
       (ProofTraceCanaryNormalEvidenceAcceptedV1 AcceptedV1))
     (MetamathProofTraceDirectNormalEvidenceV1 AcceptedV1)))

!(collapse
   (let $_proof
     (gslt:ProofTraceCanaryCompressedEvidenceAcceptedV1
       (ProofTraceCanaryCompressedEvidenceAcceptedV1 AcceptedV1))
     (MetamathProofTraceDirectCompressedEvidenceV1 AcceptedV1)))

!(collapse
   (let $_proof
     (gslt:ProofTraceCanaryWrongTargetAcceptedV1
       (ProofTraceCanaryWrongTargetAcceptedV1 UnexpectedV1))
     (MetamathProofTraceDirectWrongTargetV1 UnexpectedV1)))
