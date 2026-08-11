; Compile an authored occurrence-fold presentation against the independently
; compiled lexical definitions and parser actions.  A production is admitted
; only when its semantic action constructs the authored node at its root.

(gslt-presentation-v1 ParserOccurrenceFoldCompilerV1
  (signature
    (operator compile-occurrence-fold-production 8)
    (operator compile-occurrence-fold-semantic-token 4)
    (operator compile-occurrence-fold-shift 2)
    (operator compile-occurrence-fold-token 2)
  )
  (equations)
  (rewrites
    (rule compile-occurrence-fold-token-v1
      (head (compile-occurrence-fold-token ?tag ?role))
      (body
        (source-fold-token ?tag ?role)
        (source-fold-role ?role)
        (compile-span-definition ?tag ?regular-definition)))

    ; A lexical value may be smaller than its recognition span.  In this
    ; certified fragment the definition returns a node on the left while the
    ; right branch is recognition-only (for example, a token separator).
    ; Retain the exact authored node-production identity so an occurrence
    ; consumer can observe that node's source interval rather than guessing
    ; a payload by trimming the outer terminal span.
    (rule compile-occurrence-fold-semantic-token-v1
      (head
        (compile-occurrence-fold-semantic-token
          ?tag ?role ?node ?label))
      (body
        (source-fold-token ?tag ?role)
        (source-fold-role ?role)
        (compile-definition
          ?tag
          (sir-definition
            ?tag
            (sir-left (sir-node ?node ?payload) ?recognition-only)))
        (compile-pack-action-program
          ?tag
          ?label
          ?arity
          (pa-apply
            node
            (pa-cons
              (pa-const ?node)
              (pa-cons ?value pa-nil)))
          ?code)))

    (rule compile-occurrence-fold-shift-v1
      (head (compile-occurrence-fold-shift ?role ?operation))
      (body
        (source-fold-shift ?role ?operation)
        (source-fold-role ?role)
        (source-fold-token ?tag ?role)
        (compile-span-definition ?tag ?regular-definition)))

    (rule compile-occurrence-fold-production-v1
      (head
        (compile-occurrence-fold-production
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
        (compile-pack-action-program
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
  ))
