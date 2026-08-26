; Extensional chronological rows consumed by the generic trace-input layer.
; A later composition may close any of these relations with authored rules;
; the remaining open relations must be supplied as finite input tuples.
; Provider-only capabilities live in separate interfaces so an executable
; composition never declares an input absent from all of its rule bodies.

(gslt-presentation-v1 CertificateGSLTTraceInputInterfaceV1
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
    (rule proof-trace-input-interface-context-count-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-trace-input-context-count-v1 2))
      (body))
    (rule proof-trace-input-interface-frame-count-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-trace-input-frame-count-v1 2))
      (body))
    (rule proof-trace-input-interface-incomplete-normal-label-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-trace-input-incomplete-normal-label-v1 1))
      (body))
  ))
