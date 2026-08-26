; A compositional document language for chronological proof articles.  Authors
; name nodes and their edges independently; this presentation elaborates the
; document to the canonical numeric article consumed by the structural checker.

(gslt-presentation-v1 CertificateGSLTArticleDocumentV1
  (signature
    (operator proof-article-document-index-v1 4)
    (operator proof-article-document-order-tail-v1 3)
    (operator proof-article-document-references-v1 4)
    (operator proof-article-document-tail-v1 4)
    (operator proof-article-document-term-list-v1 4)
    (operator proof-article-document-term-v1 4)
    (operator source-proof-article-document-v1 7)
    (operator source-proof-article-canonicalize-v1 2)
    (operator source-proof-article-last-v1 3)
    (operator source-proof-article-next-v1 4)
    (operator source-proof-article-node-v1 6)
    (operator source-proof-article-order-v1 3)
    (operator source-proof-article-term-v1 4)
    (operator source-proof-node-ref-v1 1)
    (operator source-proof-term-ref-v1 1)
  )
  (equations)
  (rewrites
    (rule proof-article-document-index-start-v1
      (head
        (proof-article-document-index-v1
          ?owner ?article ?start proof-zero-v1))
      (body
        (source-proof-article-document-v1
          ?owner ?article ?context ?start ?root ?target ?policy)))
    (rule proof-article-document-order-start-v1
      (head
        (proof-article-document-order-tail-v1
          ?owner ?article ?order))
      (body
        (source-proof-article-order-v1 ?owner ?article ?order)))
    (rule proof-article-document-order-next-v1
      (head
        (source-proof-article-next-v1
          ?owner ?article ?current ?next))
      (body
        (proof-article-document-order-tail-v1
          ?owner ?article
          (proof-list-cons-v1
            ?current (proof-list-cons-v1 ?next ?rest)))))
    (rule proof-article-document-order-recurse-v1
      (head
        (proof-article-document-order-tail-v1
          ?owner ?article (proof-list-cons-v1 ?next ?rest)))
      (body
        (proof-article-document-order-tail-v1
          ?owner ?article
          (proof-list-cons-v1
            ?current (proof-list-cons-v1 ?next ?rest)))))
    (rule proof-article-document-order-last-v1
      (head
        (source-proof-article-last-v1 ?owner ?article ?last))
      (body
        (proof-article-document-order-tail-v1
          ?owner ?article
          (proof-list-cons-v1 ?last proof-list-nil-v1))))
    (rule proof-article-document-index-next-v1
      (head
        (proof-article-document-index-v1
          ?owner ?article ?next (proof-succ-v1 ?index)))
      (body
        (proof-article-document-index-v1
          ?owner ?article ?current ?index)
        (source-proof-article-next-v1
          ?owner ?article ?current ?next)))

    (rule proof-article-document-term-reference-v1
      (head
        (proof-article-document-term-v1
          ?owner ?article (source-proof-term-ref-v1 ?name) ?compiled))
      (body
        (source-proof-article-term-v1 ?owner ?article ?name ?source)
        (proof-article-document-term-v1
          ?owner ?article ?source ?compiled)))
    (rule proof-article-document-term-apply-v1
      (head
        (proof-article-document-term-v1
          ?owner ?article
          (proof-apply-v1 ?name ?source-arguments)
          (proof-apply-v1 ?name ?compiled-arguments)))
      (body
        (proof-article-document-term-list-v1
          ?owner ?article ?source-arguments ?compiled-arguments)))
    (rule proof-article-document-term-bvar-v1
      (head
        (proof-article-document-term-v1
          ?owner ?article (proof-bvar-v1 ?index) (proof-bvar-v1 ?index)))
      (body (proof-nat ?index)))
    (rule proof-article-document-term-fvar-v1
      (head
        (proof-article-document-term-v1
          ?owner ?article (proof-fvar-v1 ?name) (proof-fvar-v1 ?name)))
      (body))
    (rule proof-article-document-term-lambda-v1
      (head
        (proof-article-document-term-v1
          ?owner ?article
          (proof-lambda-v1 ?source-body)
          (proof-lambda-v1 ?compiled-body)))
      (body
        (proof-article-document-term-v1
          ?owner ?article ?source-body ?compiled-body)))
    (rule proof-article-document-term-multi-lambda-v1
      (head
        (proof-article-document-term-v1
          ?owner ?article
          (proof-multi-lambda-v1 ?arity ?source-body)
          (proof-multi-lambda-v1 ?arity ?compiled-body)))
      (body
        (proof-nat ?arity)
        (proof-article-document-term-v1
          ?owner ?article ?source-body ?compiled-body)))
    (rule proof-article-document-term-substitution-v1
      (head
        (proof-article-document-term-v1
          ?owner ?article
          (proof-subst-v1 ?source-body ?source-replacement)
          (proof-subst-v1 ?compiled-body ?compiled-replacement)))
      (body
        (proof-article-document-term-v1
          ?owner ?article ?source-body ?compiled-body)
        (proof-article-document-term-v1
          ?owner ?article ?source-replacement ?compiled-replacement)))
    (rule proof-article-document-term-collection-v1
      (head
        (proof-article-document-term-v1
          ?owner ?article
          (proof-collection-v1 ?kind ?source-elements)
          (proof-collection-v1 ?kind ?compiled-elements)))
      (body
        (proof-article-document-term-list-v1
          ?owner ?article ?source-elements ?compiled-elements)))
    (rule proof-article-document-term-list-nil-v1
      (head
        (proof-article-document-term-list-v1
          ?owner ?article proof-list-nil-v1 proof-list-nil-v1))
      (body))
    (rule proof-article-document-term-list-cons-v1
      (head
        (proof-article-document-term-list-v1
          ?owner ?article
          (proof-list-cons-v1 ?source-head ?source-rest)
          (proof-list-cons-v1 ?compiled-head ?compiled-rest)))
      (body
        (proof-article-document-term-v1
          ?owner ?article ?source-head ?compiled-head)
        (proof-article-document-term-list-v1
          ?owner ?article ?source-rest ?compiled-rest)))

    (rule proof-article-document-references-nil-v1
      (head
        (proof-article-document-references-v1
          ?owner ?article proof-list-nil-v1 proof-list-nil-v1))
      (body))
    (rule proof-article-document-references-node-v1
      (head
        (proof-article-document-references-v1
          ?owner ?article
          (proof-list-cons-v1
            (source-proof-node-ref-v1 ?symbol) ?source-rest)
          (proof-list-cons-v1
            (proof-node-ref-v1 ?index) ?compiled-rest)))
      (body
        (proof-article-document-index-v1
          ?owner ?article ?symbol ?index)
        (proof-article-document-references-v1
          ?owner ?article ?source-rest ?compiled-rest)))
    (rule proof-article-document-references-premise-v1
      (head
        (proof-article-document-references-v1
          ?owner ?article
          (proof-list-cons-v1
            (proof-premise-ref-v1 ?index) ?source-rest)
          (proof-list-cons-v1
            (proof-premise-ref-v1 ?index) ?compiled-rest)))
      (body
        (proof-nat ?index)
        (proof-article-document-references-v1
          ?owner ?article ?source-rest ?compiled-rest)))

    (rule proof-article-document-tail-last-v1
      (head
        (proof-article-document-tail-v1
          ?owner ?article ?symbol
          (proof-list-cons-v1
            (proof-node-v1 ?index ?rule ?arguments ?references)
            proof-list-nil-v1)))
      (body
        (source-proof-article-node-v1
          ?owner ?article ?symbol ?rule ?source-arguments ?source-references)
        (proof-article-document-index-v1
          ?owner ?article ?symbol ?index)
        (proof-article-document-term-list-v1
          ?owner ?article ?source-arguments ?arguments)
        (proof-article-document-references-v1
          ?owner ?article ?source-references ?references)
        (source-proof-article-last-v1 ?owner ?article ?symbol)))
    (rule proof-article-document-tail-next-v1
      (head
        (proof-article-document-tail-v1
          ?owner ?article ?symbol
          (proof-list-cons-v1
            (proof-node-v1 ?index ?rule ?arguments ?references) ?tail)))
      (body
        (source-proof-article-node-v1
          ?owner ?article ?symbol ?rule ?source-arguments ?source-references)
        (proof-article-document-index-v1
          ?owner ?article ?symbol ?index)
        (proof-article-document-term-list-v1
          ?owner ?article ?source-arguments ?arguments)
        (proof-article-document-references-v1
          ?owner ?article ?source-references ?references)
        (source-proof-article-next-v1
          ?owner ?article ?symbol ?next)
        (proof-article-document-tail-v1
          ?owner ?article ?next ?tail)))

    (rule proof-article-document-elaborate-v1
      (head
        (source-proof-article
          ?owner ?article ?context ?nodes ?root-index ?target ?policy))
      (body
        (source-proof-article-canonicalize-v1 ?owner ?article)
        (source-proof-article-document-v1
          ?owner ?article ?source-context ?start ?root ?source-target ?policy)
        (proof-article-document-term-list-v1
          ?owner ?article ?source-context ?context)
        (proof-article-document-tail-v1
          ?owner ?article ?start ?nodes)
        (proof-article-document-index-v1
          ?owner ?article ?root ?root-index)
        (proof-article-document-term-v1
          ?owner ?article ?source-target ?target)))
  ))
