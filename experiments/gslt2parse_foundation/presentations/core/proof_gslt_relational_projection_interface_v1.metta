; Finite store rows consumed by the generic proof-relation projection.  The
; interface declares relation shape only; table selection and row meaning stay
; in the authored projection GSLT.

(gslt-presentation-v1 ProofGSLTRelationalProjectionInterfaceV1
  (signature
    (operator oslf-external-relation-decl-v1 2)
  )
  (equations)
  (rewrites
    (rule proof-relational-interface-request-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-relational-request-v1 4))
      (body))
    (rule proof-relational-interface-row2-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-relational-row2-v1 3))
      (body))
    (rule proof-relational-interface-row3-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-relational-row3-v1 4))
      (body))
    (rule proof-relational-interface-row4-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-relational-row4-v1 5))
      (body))
    (rule proof-relational-interface-value-different-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-relational-value-different-v1 2))
      (body))
    (rule proof-relational-interface-value-list-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-relational-value-list-v1 3))
      (body))
  ))
