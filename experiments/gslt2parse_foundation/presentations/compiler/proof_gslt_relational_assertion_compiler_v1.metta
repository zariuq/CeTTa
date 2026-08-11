; Lower an admitted relational assertion bridge to a compact ABI.  Native
; loading binds table names to numeric identities from the generated state
; plan and refuses missing, duplicate, or shape-incompatible roles.

(gslt-presentation-v1 ProofGSLTRelationalAssertionCompilerV1
  (signature
    (operator proof-sequence-relational-artifact-v1 1)
    (operator proof-sequence-relational-identity-v1 2)
    (operator proof-sequence-relational-selector-v1 3)
    (operator proof-sequence-relational-table-v1 5)
  )
  (equations)
  (rewrites
    (rule compile-proof-sequence-relational-identity-v1
      (head
        (proof-sequence-relational-artifact-v1
          (proof-sequence-relational-identity-v1 ?owner ?base)))
      (body
        (proof-sequence-relational-layer-valid ?owner ?base)))

    (rule compile-proof-sequence-relational-table-v1
      (head
        (proof-sequence-relational-artifact-v1
          (proof-sequence-relational-table-v1
            ?owner ?role ?table ?arity ?key-arity)))
      (body
        (proof-sequence-relational-table-role-valid
          ?owner ?role ?table ?arity ?key-arity)))

    (rule compile-proof-sequence-relational-selector-v1
      (head
        (proof-sequence-relational-artifact-v1
          (proof-sequence-relational-selector-v1
            ?owner ?role ?literal)))
      (body
        (proof-sequence-relational-selector-role-valid
          ?owner ?role ?literal)))
  ))
