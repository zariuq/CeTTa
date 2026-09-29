(gslt-presentation-v1 DeclDuplicateRefV1
  (signature
    (operator metta-equation 2)
    (operator node 2)
    (operator decl:app-of 1)
    (operator decl:ref 1)
    (operator decl:name-of 1)
  )
  (equations)
  (rewrites
    (rule eq-ref-again
      (head (metta-equation
        (decl:app-of (node ref ?other))
        (decl:ref (decl:name-of ?other))))
      (body))
  ))
