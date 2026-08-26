; Uniform service entry for complete and explicitly incomplete proof traces.
; The request constructor selects the authored relation; the runtime sees one
; ordinary query entry and contains no proof-format dispatch.

(gslt-presentation-v1 CertificateGSLTTraceServiceV1
  (signature
    (operator ProofTraceServiceV1 1)
  )
  (equations)
  (rewrites
    (rule proof-trace-service-normal-v1
      (head
        (ProofTraceServiceV1
          (ProofTraceInputVerifyNormalV1 ?request ?labels)))
      (body
        (ProofTraceInputVerifyNormalV1 ?request ?labels)))
    (rule proof-trace-service-compressed-v1
      (head
        (ProofTraceServiceV1
          (ProofTraceInputVerifyCompressedV1
            ?request ?initial-labels ?explicit-labels ?codes)))
      (body
        (ProofTraceInputVerifyCompressedV1
          ?request ?initial-labels ?explicit-labels ?codes)))
    (rule proof-trace-service-incomplete-normal-v1
      (head
        (ProofTraceServiceV1
          (ProofTraceInputAcceptIncompleteNormalV1 ?request ?labels)))
      (body
        (ProofTraceInputAcceptIncompleteNormalV1 ?request ?labels)))
    (rule proof-trace-service-incomplete-compressed-v1
      (head
        (ProofTraceServiceV1
          (ProofTraceInputAcceptIncompleteCompressedV1
            ?request ?initial-labels ?explicit-labels ?codes)))
      (body
        (ProofTraceInputAcceptIncompleteCompressedV1
          ?request ?initial-labels ?explicit-labels ?codes)))
  ))
