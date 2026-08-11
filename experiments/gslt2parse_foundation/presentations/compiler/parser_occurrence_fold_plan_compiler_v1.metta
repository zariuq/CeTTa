; Lower proof-relevant occurrence declarations against a normalized
; ParserPack.  Source-fiber facts join authored source productions to their
; normalized value productions; the resulting records are executable plan
; inputs, not language policy embedded in the runtime.

(gslt-presentation-v1 ParserOccurrenceFoldPlanCompilerV1
  (signature
    (operator occurrence-fold-production-v1 4)
    (operator occurrence-fold-shift-v1 2)
    (operator occurrence-fold-token-v1 2)
    (operator occurrence-fold-token-v3 5)
  )
  (equations)
  (rewrites
    (rule lower-occurrence-fold-shift-v1
      (head (occurrence-fold-shift-v1 ?role ?operation))
      (body
        (compile-occurrence-fold-shift ?role ?operation)))

    ; Recognition-only tokens in this fold are the tokens with shift-time
    ; operations.  Value-bearing tokens are lowered by the v3 rule below.
    (rule lower-occurrence-fold-shift-token-v1
      (head (occurrence-fold-token-v1 ?tag ?role))
      (body
        (compile-occurrence-fold-token ?tag ?role)
        (compile-occurrence-fold-shift ?role ?operation)))

    (rule lower-occurrence-fold-value-token-v1
      (head
        (occurrence-fold-token-v3
          ?tag ?role ?node ?source-label ?normalized-label))
      (body
        (compile-occurrence-fold-semantic-token
          ?tag ?role ?node ?source-label)
        (compile-pack-source-occurrence
          ?normalized-label ?source-label ?occurrence)
        (compile-pack-action-program
          ?normalized-owner
          ?normalized-label
          ?normalized-arity
          (pa-apply
            node
            (pa-cons
              (pa-const ?node)
              (pa-cons ?value pa-nil)))
          ?normalized-code)))

    (rule lower-occurrence-fold-production-v1
      (head
        (occurrence-fold-production-v1
          ?normalized-label ?node ?operation ?contract))
      (body
        (compile-occurrence-fold-production
          ?normalized-owner
          ?normalized-label
          ?normalized-arity
          ?node
          ?operation
          ?contract
          ?normalized-action
          ?normalized-code)
        (compile-pack-source-occurrence
          ?normalized-label ?source-label ?occurrence)))
  ))
