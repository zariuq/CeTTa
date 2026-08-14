; Neutral chronological input ABI for the trace compiler.  Generated guest
; projections provide ordered context/frame rows and primitive token facts;
; these rules alone assemble declarations, instructions, and final verdicts.

(gslt-presentation-v1 ProofGSLTTraceInputV1
  (signature
    (operator ProofTraceInputAssertionV1 3)
    (operator ProofTraceInputBuildApartnessV1 4)
    (operator ProofTraceInputBuildContextV1 4)
    (operator ProofTraceInputBuildFrameV1 4)
    (operator ProofTraceInputAcceptIncompleteCompressedV1 4)
    (operator ProofTraceInputAcceptIncompleteNormalV1 2)
    (operator ProofTraceInputVerifyCompressedV1 4)
    (operator ProofTraceInputVerifyNormalV1 2)
    (operator source-proof-trace-input-apartness-count-v1 2)
    (operator source-proof-trace-input-apartness-v1 4)
    (operator source-proof-trace-input-assertion-v1 3)
    (operator source-proof-trace-input-context-count-v1 2)
    (operator source-proof-trace-input-context-v1 3)
    (operator source-proof-trace-input-current-apart-v1 2)
    (operator source-proof-trace-input-frame-count-v1 2)
    (operator source-proof-trace-input-frame-v1 3)
    (operator source-proof-trace-input-label-different-v1 2)
    (operator source-proof-trace-input-push-label-v1 3)
    (operator source-proof-trace-input-request-v1 2)
    (operator source-proof-trace-input-token-different-v1 2)
    (operator source-proof-trace-input-token-literal-v1 1)
    (operator source-proof-trace-input-token-variable-v1 1)
  )
  (equations)
  (rewrites
    (rule proof-trace-input-token-literal-v1
      (head (ProofTokenLiteralV1 ?token))
      (body
        (source-proof-trace-input-token-literal-v1 ?token)))
    (rule proof-trace-input-token-variable-v1
      (head (ProofTokenVariableV1 ?token))
      (body
        (source-proof-trace-input-token-variable-v1 ?token)))
    (rule proof-trace-input-token-different-v1
      (head (ProofTokenDifferentV1 ?left ?right))
      (body
        (source-proof-trace-input-token-different-v1 ?left ?right)))
    (rule proof-trace-input-current-apart-forward-v1
      (head (ProofTokenApartV1 ?left ?right))
      (body
        (source-proof-trace-input-current-apart-v1 ?left ?right)))
    (rule proof-trace-input-current-apart-reverse-v1
      (head (ProofTokenApartV1 ?right ?left))
      (body
        (source-proof-trace-input-current-apart-v1 ?left ?right)))

    (rule proof-trace-input-build-context-nil-v1
      (head
        (ProofTraceInputBuildContextV1
          ?request ?count ?count ProofTraceContextNilV1))
      (body))
    (rule proof-trace-input-build-context-cons-v1
      (head
        (ProofTraceInputBuildContextV1
          ?request ?index ?count
          (ProofTraceContextConsV1 ?formula ?context)))
      (body
        (source-proof-trace-input-context-v1
          ?request ?index ?formula)
        (ProofTraceInputBuildContextV1
          ?request (ProofTraceNatSuccV1 ?index) ?count ?context)))

    (rule proof-trace-input-build-frame-nil-v1
      (head
        (ProofTraceInputBuildFrameV1
          ?assertion ?count ?count ProofTraceFrameNilV1))
      (body))
    (rule proof-trace-input-build-frame-cons-v1
      (head
        (ProofTraceInputBuildFrameV1
          ?assertion ?index ?count
          (ProofTraceFrameConsV1 ?entry ?frame)))
      (body
        (source-proof-trace-input-frame-v1
          ?assertion ?index ?entry)
        (ProofTraceInputBuildFrameV1
          ?assertion (ProofTraceNatSuccV1 ?index) ?count ?frame)))

    (rule proof-trace-input-build-apartness-nil-v1
      (head
        (ProofTraceInputBuildApartnessV1
          ?assertion ?count ?count ProofAssertionListNilV1))
      (body))
    (rule proof-trace-input-build-apartness-cons-v1
      (head
        (ProofTraceInputBuildApartnessV1
          ?assertion ?index ?count
          (ProofAssertionListConsV1
            (ProofAssertionDisjointV1 ?left ?right) ?apartness)))
      (body
        (source-proof-trace-input-apartness-v1
          ?assertion ?index ?left ?right)
        (ProofTraceInputBuildApartnessV1
          ?assertion (ProofTraceNatSuccV1 ?index) ?count ?apartness)))

    (rule proof-trace-input-assertion-v1
      (head
        (ProofTraceInputAssertionV1
          ?label
          (ProofAssertionV1
            ?variables ?essentials ?conclusion-type
            ?conclusion-template ?apartness)
          ?frame))
      (body
        (source-proof-trace-input-assertion-v1
          ?label ?conclusion-type ?conclusion-template)
        (source-proof-trace-input-frame-count-v1
          ?label ?frame-count)
        (source-proof-trace-input-apartness-count-v1
          ?label ?apartness-count)
        (ProofTraceInputBuildFrameV1
          ?label ProofTraceNatZeroV1 ?frame-count ?frame)
        (ProofTraceFrameProjectV1 ?frame ?variables ?essentials)
        (ProofTraceInputBuildApartnessV1
          ?label ProofTraceNatZeroV1 ?apartness-count ?apartness)))
    (rule proof-trace-input-assertion-declared-v1
      (head (ProofAssertionDeclaredV1 ?assertion))
      (body
        (ProofTraceInputAssertionV1 ?label ?assertion ?frame)))

    (rule proof-trace-input-push-instruction-v1
      (head
        (source-proof-trace-label-instruction-v1
          ?label (ProofTracePushV1 ?formula)))
      (body
        (source-proof-trace-input-request-v1 ?request ?target)
        (source-proof-trace-input-push-label-v1
          ?request ?label ?formula)))
    (rule proof-trace-input-apply-instruction-v1
      (head
        (source-proof-trace-label-instruction-v1
          ?label (ProofTraceApplyV1 ?assertion ?frame)))
      (body
        (ProofTraceInputAssertionV1 ?label ?assertion ?frame)))

    (rule proof-trace-input-verify-normal-v1
      (head
        (ProofTraceInputVerifyNormalV1 ?request ?labels))
      (body
        (source-proof-trace-input-request-v1 ?request ?target)
        (source-proof-trace-input-context-count-v1
          ?request ?context-count)
        (ProofTraceInputBuildContextV1
          ?request ProofTraceNatZeroV1 ?context-count ?context)
        (ProofTraceCompileNormalAcceptedV1
          ?context ?labels ?target)))
    (rule proof-trace-input-accept-incomplete-normal-v1
      (head
        (ProofTraceInputAcceptIncompleteNormalV1 ?request ?labels))
      (body
        (source-proof-trace-input-request-v1 ?request ?target)
        (source-proof-trace-input-context-count-v1
          ?request ?context-count)
        (ProofTraceInputBuildContextV1
          ?request ProofTraceNatZeroV1 ?context-count ?context)
        (ProofTraceCompileNormalIncompleteAcceptedV1
          ?context ?labels ?target)))
    (rule proof-trace-input-verify-compressed-v1
      (head
        (ProofTraceInputVerifyCompressedV1
          ?request ?initial-labels ?explicit-labels ?codes))
      (body
        (source-proof-trace-input-request-v1 ?request ?target)
        (source-proof-trace-input-context-count-v1
          ?request ?context-count)
        (ProofTraceInputBuildContextV1
          ?request ProofTraceNatZeroV1 ?context-count ?context)
        (ProofTraceCompileCompressedAcceptedV1
          ?context ?initial-labels ?explicit-labels ?codes ?target)))
    (rule proof-trace-input-accept-incomplete-compressed-v1
      (head
        (ProofTraceInputAcceptIncompleteCompressedV1
          ?request ?initial-labels ?explicit-labels ?codes))
      (body
        (source-proof-trace-input-request-v1 ?request ?target)
        (source-proof-trace-input-context-count-v1
          ?request ?context-count)
        (ProofTraceInputBuildContextV1
          ?request ProofTraceNatZeroV1 ?context-count ?context)
        (ProofTraceCompileCompressedIncompleteAcceptedV1
          ?context ?initial-labels ?explicit-labels ?codes ?target)))
  ))
