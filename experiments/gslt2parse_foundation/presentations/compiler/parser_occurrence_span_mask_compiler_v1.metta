; Compose occurrence-fold value bindings with the regular semantic-mask
; compiler.  The wrapper records keep the dependency on the authored fold
; visible until the final projection step emits the ordinary mask ABI.

(gslt-presentation-v1 ParserOccurrenceSpanMaskCompilerV1
  (signature
    (operator compile-occurrence-span-mask-link-v1 1)
    (operator compile-occurrence-span-mask-nfa-v1 1)
  )
  (equations)
  (rewrites
    (rule compile-occurrence-span-mask-nfa-v1
      (head
        (compile-occurrence-span-mask-nfa-v1
          (compile-semantic-mask-nfa ?node ?edge)))
      (body
        (occurrence-fold-token-v3
          ?tag ?role ?node ?source-label ?normalized-label)
        (compile-semantic-mask-nfa ?node ?edge)))

    (rule compile-occurrence-span-mask-link-v1
      (head
        (compile-occurrence-span-mask-link-v1
          (compile-semantic-mask-root-link ?definition ?node)))
      (body
        (occurrence-fold-token-v3
          ?tag ?role ?node ?source-label ?normalized-label)
        (compile-semantic-mask-root-link ?definition ?node)))
  ))
