; Compile a relational projection layer to a variable-length descriptor.
; The artifact carries every table and selector assignment; a native executor
; may iterate these records but must not supply an additional role inventory.

(gslt-presentation-v1 ProofGSLTRelationalProjectionCompilerV1
  (signature
    (operator proof-relational-projection-artifact-v1 1)
    (operator proof-relational-projection-identity-v1 2)
    (operator proof-relational-projection-selector-v1 3)
    (operator proof-relational-projection-table-v1 5)
  )
  (equations)
  (rewrites
    (rule compile-proof-relational-projection-identity-v1
      (head
        (proof-relational-projection-artifact-v1
          (proof-relational-projection-identity-v1 ?owner ?base)))
      (body
        (proof-relational-projection-layer-valid-v1 ?owner ?base)))
    (rule compile-proof-relational-projection-table-v1
      (head
        (proof-relational-projection-artifact-v1
          (proof-relational-projection-table-v1
            ?owner ?role ?table ?arity ?key-arity)))
      (body
        (proof-relational-projection-table-role-valid-v1
          ?owner ?role ?table ?arity ?key-arity)))
    (rule compile-proof-relational-projection-selector-v1
      (head
        (proof-relational-projection-artifact-v1
          (proof-relational-projection-selector-v1
            ?owner ?role ?literal)))
      (body
        (proof-relational-projection-selector-role-valid-v1
          ?owner ?role ?literal)))
  ))
