; Negative: a mode digit other than 0 and 1.
(gslt-presentation-v1 CapabilityBadDigitsV1
  (signature
    (operator UseV1 1)
    (operator cap-digits 2)
    (operator oslf-external-relation-decl-v1 2)
    (operator oslf-external-relation-modes-v1 2))
  (equations)
  (rewrites
    (rule cap-digits-capability (head (oslf-external-relation-decl-v1 cap-digits 2)) (body))
    (rule cap-digits-modes (head (oslf-external-relation-modes-v1 cap-digits "1x")) (body))
    (rule use (head (UseV1 ?y)) (body (cap-digits 1 ?y)))))
