; Join source-authored operations to the language-neutral indexed-checker
; effect algebra.  Node and shift rules are separate so every emitted effect
; retains a concrete occurrence source.

(gslt-presentation-v1 IndexedCheckerFoldCompilerV1
  (signature
    (operator compile-indexed-checker-effect 2)
    (operator compile-indexed-checker-policy 2)
  )
  (equations)
  (rewrites
    (rule compile-indexed-checker-node-effect-v1
      (head (compile-indexed-checker-effect ?operation ?effect))
      (body
        (source-checker-effect ?operation ?effect)
        (indexed-checker-effect ?effect)
        (source-fold-node ?node ?operation ?contract)
        (fold-contract-valid ?contract)))

    (rule compile-indexed-checker-shift-effect-v1
      (head (compile-indexed-checker-effect ?operation ?effect))
      (body
        (source-checker-effect ?operation ?effect)
        (indexed-checker-effect ?effect)
        (source-fold-shift ?role ?operation)
        (source-fold-role ?role)))

    (rule compile-indexed-checker-policy-v1
      (head (compile-indexed-checker-policy ?key ?value))
      (body
        (source-checker-policy ?key ?value)
        (indexed-checker-policy-key ?key)
        (indexed-checker-policy-value ?value)))
  ))
