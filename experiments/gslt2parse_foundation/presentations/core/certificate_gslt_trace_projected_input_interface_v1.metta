; Provider-only rows for standalone trace compositions.  A relational proof
; machine derives these relations from its source-owned state projection, so
; it must not import this interface and thereby create a second authority.

(gslt-presentation-v1 CertificateGSLTTraceProjectedInputInterfaceV1
  (signature
    (operator oslf-external-relation-decl-v1 2)
  )
  (equations)
  (rewrites
    (rule proof-trace-projected-input-interface-apartness-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-trace-input-apartness-v1 4))
      (body))
    (rule proof-trace-projected-input-interface-assertion-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-trace-input-assertion-v1 3))
      (body))
    (rule proof-trace-projected-input-interface-context-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-trace-input-context-v1 3))
      (body))
    (rule proof-trace-projected-input-interface-frame-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-trace-input-frame-v1 3))
      (body))
    (rule proof-trace-projected-input-interface-label-different-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-trace-input-label-different-v1 2))
      (body))
    (rule proof-trace-projected-input-interface-push-label-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-trace-input-push-label-v1 3))
      (body))
    (rule proof-trace-projected-input-interface-request-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-trace-input-request-v1 2))
      (body))
    (rule proof-trace-projected-input-interface-token-different-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-trace-input-token-different-v1 2))
      (body))
    (rule proof-trace-projected-input-interface-token-class-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-trace-input-token-class-v1 2))
      (body))
    (rule proof-trace-projected-input-interface-token-compare-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-trace-input-token-compare-v1 3))
      (body))
    (rule proof-trace-projected-input-interface-token-literal-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-trace-input-token-literal-v1 1))
      (body))
    (rule proof-trace-projected-input-interface-token-variable-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-trace-input-token-variable-v1 1))
      (body))
  ))
