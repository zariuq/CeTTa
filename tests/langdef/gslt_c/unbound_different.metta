; different on a variable no premise has bound: refused (a ground relation
; is asked of ground arguments only).
(gslt-presentation-v1 GsltCUnboundDifferentV1
  (signature
    (operator U 2)
    (operator Yes 0))
  (equations)
  (rewrites
    (rule u
      (head (U ?x Yes))
      (body (different ?y ?x)))))
