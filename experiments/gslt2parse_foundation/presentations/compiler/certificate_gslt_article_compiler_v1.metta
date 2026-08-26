; Compile admitted structural proof extensions and articles to one canonical
; answer stream.  The native loader performs representation, uniqueness, and
; resource admission before the generic checker executes the records.

(gslt-presentation-v1 CertificateGSLTArticleCompilerV1
  (signature
    (operator proof-article-v1 7)
    (operator proof-artifact-v1 1)
    (operator proof-base-constructor-v1 4)
    (operator proof-calculus-constructor-v1 3)
    (operator proof-capability-v1 2)
    (operator proof-conversion-v1 3)
    (operator proof-extension-v1 2)
    (operator proof-judgment-v1 3)
    (operator proof-rule-v1 6)
  )
  (equations)
  (rewrites
    (rule compile-proof-extension-v1
      (head
        (proof-artifact-v1 (proof-extension-v1 ?owner ?base)))
      (body (source-proof-extension ?owner ?base)))
    (rule compile-proof-base-constructor-v1
      (head
        (proof-artifact-v1
          (proof-base-constructor-v1 ?owner ?base ?name ?arity)))
      (body
        (source-proof-extension ?owner ?base)
        (source-proof-base-constructor ?base ?name ?arity)
        (proof-nat ?arity)))
    (rule compile-proof-calculus-constructor-v1
      (head
        (proof-artifact-v1
          (proof-calculus-constructor-v1 ?owner ?name ?arity)))
      (body
        (source-proof-extension ?owner ?base)
        (source-proof-calculus-constructor ?owner ?name ?arity)
        (proof-nat ?arity)))
    (rule compile-proof-judgment-v1
      (head
        (proof-artifact-v1
          (proof-judgment-v1 ?owner ?head ?arity)))
      (body
        (source-proof-extension ?owner ?base)
        (source-proof-judgment ?owner ?head ?arity)
        (proof-nat ?arity)))
    (rule compile-proof-capability-v1
      (head
        (proof-artifact-v1
          (proof-capability-v1 ?owner ?capability)))
      (body (proof-capability-declared ?owner ?capability)))
    (rule compile-proof-conversion-v1
      (head
        (proof-artifact-v1
          (proof-conversion-v1 ?owner ?judgment ?version)))
      (body
        (source-proof-conversion ?owner ?judgment ?version)
        (proof-capability-declared
          ?owner proof-cap-conversion-declaration-v1)
        (source-proof-judgment
          ?owner ?judgment
          (proof-succ-v1 (proof-succ-v1 proof-zero-v1)))))
    (rule compile-proof-rule-v1
      (head
        (proof-artifact-v1
          (proof-rule-v1
            ?owner ?id ?formals ?premises ?conclusion
            ?compiled-conditions)))
      (body
        (proof-rule-admitted
          ?owner ?id ?formals ?premises ?conclusion
          ?compiled-conditions)))
    (rule compile-proof-article-v1
      (head
        (proof-artifact-v1
          (proof-article-v1
            ?owner ?article-id ?context ?nodes ?root ?target
            ?root-policy)))
      (body
        (proof-article-admitted
          ?owner ?article-id ?context ?nodes ?root ?target
          ?root-policy)))
  ))
