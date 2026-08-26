; Extensional inputs consumed by the generic article calculus itself.  These
; declarations authorize finite relation rows only; they do not name or carry
; a native implementation.  Optional article-compiler capabilities live in
; separate interfaces so a composition never declares an input it cannot use.

(gslt-presentation-v1 CertificateGSLTArticleInterfaceV1
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
    (rule proof-article-interface-extension-v1
      (head
        (oslf-external-relation-decl-v1 source-proof-extension 2))
      (body))
    (rule proof-article-interface-judgment-v1
      (head
        (oslf-external-relation-decl-v1 source-proof-judgment 3))
      (body))
    (rule proof-article-interface-rule-v1
      (head
        (oslf-external-relation-decl-v1 source-proof-rule 6))
      (body))
    (rule proof-article-interface-sequence-functional-layer-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-sequence-functional-layer 2))
      (body))
    (rule proof-article-interface-sequence-relational-layer-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-sequence-relational-layer 2))
      (body))
  ))
