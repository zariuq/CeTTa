; Rules sharing program A's host: DoubleB asks cap-double; ZeroB holds only
; where cap-zero gives ZeroV1 (a constant where the host's output goes).
(gslt-presentation-v1 CapabilityRulesBV1
  (signature
    (operator DoubleB 2)
    (operator ZeroB 2)
    (operator cap-double 2)
    (operator cap-zero 2))
  (equations)
  (rewrites
    (rule double-b
      (head (DoubleB ?x ?d))
      (body (cap-double ?x ?d)))
    (rule zero-b
      (head (ZeroB ?x ZeroSeenV1))
      (body (cap-zero ?x ZeroV1)))))
