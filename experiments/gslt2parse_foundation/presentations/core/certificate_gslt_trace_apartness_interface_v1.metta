; Provider-only apartness decisions for standalone trace compositions.  A
; relational proof machine closes these relations from its authored state
; projection instead, so it must not import this interface.

(gslt-presentation-v1 CertificateGSLTTraceApartnessInterfaceV1
  (signature
    (operator oslf-external-relation-decl-v1 2)
  )
  (equations)
  (rewrites
    (rule proof-trace-apartness-interface-current-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-trace-input-current-apart-v1 2))
      (body))
    (rule proof-trace-apartness-interface-decision-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-trace-input-current-apart-decision-v1 3))
      (body))
  ))
