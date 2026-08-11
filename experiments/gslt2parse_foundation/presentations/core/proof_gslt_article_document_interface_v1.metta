; Extensional document rows consumed by canonical article elaboration.

(gslt-presentation-v1 ProofGSLTArticleDocumentInterfaceV1
  (signature
    (operator oslf-external-relation-decl-v1 2)
  )
  (equations)
  (rewrites
    (rule proof-article-document-interface-canonicalize-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-article-canonicalize-v1 2))
      (body))
    (rule proof-article-document-interface-document-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-article-document-v1 7))
      (body))
    (rule proof-article-document-interface-node-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-article-node-v1 6))
      (body))
    (rule proof-article-document-interface-order-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-article-order-v1 3))
      (body))
    (rule proof-article-document-interface-term-v1
      (head
        (oslf-external-relation-decl-v1
          source-proof-article-term-v1 4))
      (body))
  ))
