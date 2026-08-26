; Finite extensional relations consumed by the first-order ProofGSLT
; denotation.  The interface declares relation shape only; a composed guest
; supplies the actual assertion, token-class, inequality, and apartness rows.

(gslt-presentation-v1 ProofGSLTFirstOrderSemanticInterfaceV1
  (signature
    (operator oslf-external-relation-decl-v1 2)
  )
  (equations)
  (rewrites
    (rule proof-first-order-interface-assertion-v1
      (head
        (oslf-external-relation-decl-v1 ProofAssertionDeclaredV1 1))
      (body))
    (rule proof-first-order-interface-token-apart-v1
      (head
        (oslf-external-relation-decl-v1 ProofTokenApartV1 2))
      (body))
    (rule proof-first-order-interface-token-different-v1
      (head
        (oslf-external-relation-decl-v1 ProofTokenDifferentV1 2))
      (body))
    (rule proof-first-order-interface-token-literal-v1
      (head
        (oslf-external-relation-decl-v1 ProofTokenLiteralV1 1))
      (body))
    (rule proof-first-order-interface-token-variable-v1
      (head
        (oslf-external-relation-decl-v1 ProofTokenVariableV1 1))
      (body))
  ))
