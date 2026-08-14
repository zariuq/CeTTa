; Lower an authored relation-store state presentation to flat executable
; records. Native admission additionally checks dense action indices, unique
; table keys, operation/role identities, and the complete artifact digest.

(gslt-presentation-v1 RelationalStateProgramCompilerV1
  (signature
    (operator state-action-v1 3)
    (operator state-final-action-v1 2)
    (operator state-proof-action-v1 3)
    (operator state-proof-machine-v1 2)
    (operator state-table-v1 4)
  )
  (equations)
  (rewrites
    (rule compile-relational-state-table-v1
      (head
        (state-table-v1 ?table ?arity ?key-arity ?lifetime))
      (body
        (source-state-table ?table ?arity ?key-arity ?lifetime)
        (source-state-table-shape ?arity ?key-arity)
        (source-state-lifetime ?lifetime)))

    (rule compile-relational-state-action-v1
      (head (state-action-v1 ?operation ?index ?action))
      (body
        (source-state-action ?operation ?index ?action)
        (state-action-valid ?operation ?action)))

    (rule compile-relational-state-proof-machine-v1
      (head (state-proof-machine-v1 ?machine ?configuration))
      (body
        (source-state-proof-machine ?machine ?configuration)
        (state-proof-machine-valid ?configuration)))

    (rule compile-relational-state-proof-action-v1
      (head (state-proof-action-v1 ?machine ?source-kind ?action))
      (body
        (source-state-proof-action ?machine ?source-kind ?action)))

    (rule compile-relational-state-final-action-v1
      (head (state-final-action-v1 ?index ?action))
      (body
        (source-state-final-action ?index ?action)
        (state-final-action-valid ?action)))
  ))
