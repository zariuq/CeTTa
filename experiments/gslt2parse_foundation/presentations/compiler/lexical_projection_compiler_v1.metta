(gslt-presentation-v1 LexicalProjectionCompilerV1
  (signature
    (operator compile-lexical-root 1)
  )
  (equations)
  (rewrites
    (rule compile-authored-lexical-root
      (head (compile-lexical-root ?root))
      (body (source-lexical-root ?root)))
  ))
