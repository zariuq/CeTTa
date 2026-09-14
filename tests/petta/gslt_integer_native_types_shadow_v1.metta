(gslt-presentation-v1 OrdinaryIntegerRelationsV1
  (signature
    (operator ground-integer-less 2)
    (operator ground-integer-not-less 2))
  (equations)
  (rewrites
    (rule ordinary-less-fact
      (head (ground-integer-less 0 2)) (body))
    (rule ordinary-not-less-fact
      (head (ground-integer-not-less 0 2)) (body))))
