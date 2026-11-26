; Rules whose values come back from a host: QuadA x is cap-double of
; cap-double of x.  Compiled with the host as a source: this program owns it.
(gslt-presentation-v1 CapabilityRulesAV1
  (signature
    (operator QuadA 2)
    (operator cap-double 2))
  (equations)
  (rewrites
    (rule quad-a
      (head (QuadA ?x ?q))
      (body (cap-double ?x ?d) (cap-double ?d ?q)))))
