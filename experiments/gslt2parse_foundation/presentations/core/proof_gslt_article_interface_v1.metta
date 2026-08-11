; Extensional inputs consumed by the generic article calculus.  These
; declarations authorize finite relation rows only; they do not name or carry
; a native implementation.

(gslt-presentation-v1 ProofGSLTArticleInterfaceV1
  (signature
    (operator oslf-external-relation-decl-v1 2)
  )
  (equations)
  (rewrites
    (rule proof-article-interface-article-v1
      (head
        (oslf-external-relation-decl-v1 source-proof-article 7))
      (body))
    (rule proof-article-interface-base-constructor-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-base-constructor 3))
      (body))
    (rule proof-article-interface-capability-v1
      (head
        (oslf-external-relation-decl-v1 source-proof-capability 2))
      (body))
  ))
