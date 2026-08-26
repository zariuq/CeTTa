; Lower independently authored named-article documents to a compact generic
; deployment ABI.  The generic loader resolves this DAG before the unchanged
; structural article checker receives the semantic article.

(gslt-presentation-v1 ProofGSLTArticleDocumentCompilerV1
  (signature
    (operator proof-article-document-v1 7)
    (operator proof-article-named-node-v1 6)
    (operator proof-article-order-v1 3)
    (operator proof-article-term-v1 4)
  )
  (equations)
  (rewrites
    (rule compile-proof-article-term-v1
      (head
        (proof-artifact-v1
          (proof-article-term-v1 ?owner ?article ?name ?term)))
      (body
        (source-proof-article-term-v1 ?owner ?article ?name ?term)))
    (rule compile-proof-article-named-node-v1
      (head
        (proof-artifact-v1
          (proof-article-named-node-v1
            ?owner ?article ?name ?rule ?arguments ?references)))
      (body
        (source-proof-article-node-v1
          ?owner ?article ?name ?rule ?arguments ?references)))
    (rule compile-proof-article-order-v1
      (head
        (proof-artifact-v1
          (proof-article-order-v1 ?owner ?article ?order)))
      (body
        (source-proof-article-order-v1 ?owner ?article ?order)))
    (rule compile-proof-article-document-v1
      (head
        (proof-artifact-v1
          (proof-article-document-v1
            ?owner ?article ?context ?start ?root ?target ?root-policy)))
      (body
        (source-proof-article-document-v1
          ?owner ?article ?context ?start ?root ?target ?root-policy)))
  ))
