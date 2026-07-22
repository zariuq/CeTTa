(gslt-presentation-v1 RelationalCFGLinkV1
  (signature
    (operator sir-rel 1)
  )
  (equations)
  (rewrites
    (rule compile-grammar-relational-state
      (head (compile-grammar ?state (sir-rel ?state)))
      (body (compile-cfg-state ?state)))
  ))
