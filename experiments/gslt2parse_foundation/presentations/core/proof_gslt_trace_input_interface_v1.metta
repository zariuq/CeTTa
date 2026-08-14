; Extensional chronological rows consumed by the generic trace-input layer.
; A later composition may close any of these relations with authored rules;
; the remaining open relations must be supplied as finite input tuples.

(gslt-presentation-v1 ProofGSLTTraceInputInterfaceV1
  (signature
    (operator oslf-external-relation-decl-v1 2)
  )
  (equations)
  (rewrites
    (rule proof-trace-input-interface-apartness-count-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-trace-input-apartness-count-v1 2))
      (body))
    (rule proof-trace-input-interface-apartness-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-trace-input-apartness-v1 4))
      (body))
    (rule proof-trace-input-interface-assertion-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-trace-input-assertion-v1 3))
      (body))
    (rule proof-trace-input-interface-context-count-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-trace-input-context-count-v1 2))
      (body))
    (rule proof-trace-input-interface-context-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-trace-input-context-v1 3))
      (body))
    (rule proof-trace-input-interface-current-apart-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-trace-input-current-apart-v1 2))
      (body))
    (rule proof-trace-input-interface-frame-count-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-trace-input-frame-count-v1 2))
      (body))
    (rule proof-trace-input-interface-frame-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-trace-input-frame-v1 3))
      (body))
    (rule proof-trace-input-interface-incomplete-compressed-code-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-trace-input-incomplete-compressed-code-v1 1))
      (body))
    (rule proof-trace-input-interface-incomplete-normal-label-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-trace-input-incomplete-normal-label-v1 1))
      (body))
    (rule proof-trace-input-interface-label-different-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-trace-input-label-different-v1 2))
      (body))
    (rule proof-trace-input-interface-push-label-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-trace-input-push-label-v1 3))
      (body))
    (rule proof-trace-input-interface-request-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-trace-input-request-v1 2))
      (body))
    (rule proof-trace-input-interface-token-different-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-trace-input-token-different-v1 2))
      (body))
    (rule proof-trace-input-interface-token-literal-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-trace-input-token-literal-v1 1))
      (body))
    (rule proof-trace-input-interface-token-variable-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-trace-input-token-variable-v1 1))
      (body))
  ))
