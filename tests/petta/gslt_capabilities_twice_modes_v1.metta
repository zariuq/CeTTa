; Negative: one capability, two mode declarations.
(gslt-presentation-v1 CapabilityTwiceModesV1
  (signature
    (operator UseV1 1)
    (operator cap-twice 2)
    (operator oslf-external-relation-decl-v1 2)
    (operator oslf-external-relation-modes-v1 2))
  (equations)
  (rewrites
    (rule cap-twice-capability (head (oslf-external-relation-decl-v1 cap-twice 2)) (body))
    (rule cap-twice-modes (head (oslf-external-relation-modes-v1 cap-twice "10")) (body))
    (rule cap-twice-modes-again (head (oslf-external-relation-modes-v1 cap-twice "11")) (body))
    (rule use (head (UseV1 ?y)) (body (cap-twice 1 ?y)))))
