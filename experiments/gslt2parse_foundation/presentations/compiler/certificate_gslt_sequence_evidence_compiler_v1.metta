; Compile the independently authored sequence-relation roles to a compact
; producer ABI.  Every emitted name is already admitted by the structural
; CertificateGSLT presentation; native evidence construction is therefore a
; producer convenience and never an alternate proof authority.

(gslt-presentation-v1 CertificateGSLTSequenceEvidenceCompilerV1
  (signature
    (operator proof-sequence-evidence-artifact-v1 1)
    (operator proof-sequence-evidence-constructor-role-v1 4)
    (operator proof-sequence-evidence-judgment-role-v1 3)
    (operator proof-sequence-evidence-rule-role-v1 3)
    (operator proof-sequence-evidence-assertion-constructor-role-v1 4)
    (operator proof-sequence-evidence-assertion-judgment-role-v1 3)
    (operator proof-sequence-evidence-assertion-rule-role-v1 3)
  )
  (equations)
  (rewrites
    (rule compile-proof-sequence-constructor-role-v1
      (head
        (proof-sequence-evidence-artifact-v1
          (proof-sequence-evidence-constructor-role-v1
            ?owner ?base ?role ?name)))
      (body
        (source-proof-extension ?owner ?base)
        (source-proof-sequence-constructor-role
          ?owner ?base ?role ?name)
        (source-proof-calculus-constructor ?owner ?name ?arity)
        (proof-nat ?arity)))
    (rule compile-proof-sequence-judgment-role-v1
      (head
        (proof-sequence-evidence-artifact-v1
          (proof-sequence-evidence-judgment-role-v1
            ?owner ?role ?name)))
      (body
        (source-proof-extension ?owner ?base)
        (source-proof-sequence-judgment-role ?owner ?role ?name)
        (source-proof-judgment ?owner ?name ?arity)
        (proof-nat ?arity)))
    (rule compile-proof-sequence-rule-role-v1
      (head
        (proof-sequence-evidence-artifact-v1
          (proof-sequence-evidence-rule-role-v1
            ?owner ?role ?name)))
      (body
        (source-proof-extension ?owner ?base)
        (source-proof-sequence-rule-role ?owner ?role ?name)
        (proof-rule-admitted
          ?owner ?name ?formals ?premises ?conclusion ?conditions)))
    (rule compile-proof-sequence-assertion-constructor-role-v1
      (head
        (proof-sequence-evidence-artifact-v1
          (proof-sequence-evidence-assertion-constructor-role-v1
            ?owner ?base ?role ?name)))
      (body
        (source-proof-extension ?owner ?base)
        (source-proof-sequence-assertion-constructor-role
          ?owner ?base ?role ?name)
        (source-proof-calculus-constructor ?owner ?name ?arity)
        (proof-nat ?arity)))
    (rule compile-proof-sequence-assertion-judgment-role-v1
      (head
        (proof-sequence-evidence-artifact-v1
          (proof-sequence-evidence-assertion-judgment-role-v1
            ?owner ?role ?name)))
      (body
        (source-proof-extension ?owner ?base)
        (source-proof-sequence-assertion-judgment-role
          ?owner ?role ?name)
        (source-proof-judgment ?owner ?name ?arity)
        (proof-nat ?arity)))
    (rule compile-proof-sequence-assertion-rule-role-v1
      (head
        (proof-sequence-evidence-artifact-v1
          (proof-sequence-evidence-assertion-rule-role-v1
            ?owner ?role ?name)))
      (body
        (source-proof-extension ?owner ?base)
        (source-proof-sequence-assertion-rule-role
          ?owner ?role ?name)
        (proof-rule-admitted
          ?owner ?name ?formals ?premises ?conclusion ?conditions)))
  ))
