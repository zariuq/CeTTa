(gslt-presentation-v1 RelationalCapabilityV1
  (signature
    (operator CapabilityV1 1)
    (operator ThroughCapabilityV1 2)
    (operator IndirectCapabilityV1 2)
    (operator z 0)
    (operator oslf-external-relation-decl-v1 2))
  (equations)
  (rewrites
    (rule capability-declaration
      (head (oslf-external-relation-decl-v1 CapabilityV1 1)) (body))
    (rule through-capability (head (ThroughCapabilityV1 z ?value))
      (body (CapabilityV1 ?value)))
    (rule open-answer (head (ThroughCapabilityV1 z ?value)) (body))
    (rule indirect-capability (head (IndirectCapabilityV1 ?tag ?value))
      (body (ThroughCapabilityV1 ?tag ?value)))))
