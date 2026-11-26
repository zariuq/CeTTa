; Negative: modes for a relation no rule declares a capability.
(gslt-presentation-v1 CapabilityUndeclaredModesV1
  (signature
    (operator UseV1 1)
    (operator cap-none 2)
    (operator oslf-external-relation-modes-v1 2))
  (equations)
  (rewrites
    (rule cap-none-modes (head (oslf-external-relation-modes-v1 cap-none "10")) (body))
    (rule use (head (UseV1 ?y)) (body (cap-none 1 ?y)))))
