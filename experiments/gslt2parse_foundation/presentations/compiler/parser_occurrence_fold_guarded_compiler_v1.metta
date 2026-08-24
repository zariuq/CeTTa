; Bind an authored occurrence fold directly to a guarded ParserPack.  Unlike
; transparent normalization, this path retains the exact source production:
; the emitted value-production label is the authored label itself.

(gslt-presentation-v1 ParserOccurrenceFoldGuardedCompilerV1
  (signature
    (operator compile-guarded-occurrence-fold-production 8)
    (operator compile-guarded-occurrence-fold-semantic-token 4)
    (operator occurrence-fold-production-v1 4)
    (operator occurrence-fold-token-v4 4)
  )
  (equations)
  (rewrites
    (rule compile-guarded-occurrence-fold-semantic-token-root-v1
      (head
        (compile-guarded-occurrence-fold-semantic-token
          ?tag ?role ?node ?label))
      (body
        (source-fold-token ?tag ?role)
        (source-fold-role ?role)
        (compile-definition
          ?tag
          (sir-definition ?tag (sir-node ?node ?payload)))
        (compile-guard-extended-action-program
          ?tag
          ?label
          ?arity
          (pa-apply
            node
            (pa-cons
              (pa-const ?node)
              (pa-cons ?value pa-nil)))
          ?code)))

    (rule compile-guarded-occurrence-fold-semantic-token-v1
      (head
        (compile-guarded-occurrence-fold-semantic-token
          ?tag ?role ?node ?label))
      (body
        (source-fold-token ?tag ?role)
        (source-fold-role ?role)
        (compile-definition
          ?tag
          (sir-definition
            ?tag
            (sir-left (sir-node ?node ?payload) ?recognition-only)))
        (compile-guard-extended-action-program
          ?tag
          ?label
          ?arity
          (pa-apply
            node
            (pa-cons
              (pa-const ?node)
              (pa-cons ?value pa-nil)))
          ?code)))

    (rule lower-guarded-occurrence-fold-semantic-token-v1
      (head
        (occurrence-fold-token-v4 ?tag ?role ?node ?label))
      (body
        (compile-guarded-occurrence-fold-semantic-token
          ?tag ?role ?node ?label)))

    (rule compile-guarded-occurrence-fold-production-v1
      (head
        (compile-guarded-occurrence-fold-production
          ?owner
          ?label
          ?arity
          ?node
          ?operation
          ?contract
          (pa-apply
            node
            (pa-cons
              (pa-const ?node)
              (pa-cons ?payload pa-nil)))
          ?code))
      (body
        (compile-guard-extended-action-program
          ?owner
          ?label
          ?arity
          (pa-apply
            node
            (pa-cons
              (pa-const ?node)
              (pa-cons ?payload pa-nil)))
          ?code)
        (source-fold-node ?node ?operation ?contract)
        (fold-contract-valid ?contract)))

    (rule lower-guarded-occurrence-fold-production-v1
      (head
        (occurrence-fold-production-v1
          ?label ?node ?operation ?contract))
      (body
        (compile-guarded-occurrence-fold-production
          ?owner
          ?label
          ?arity
          ?node
          ?operation
          ?contract
          ?action
          ?code)))
  ))
