(gslt-presentation-v1 BroadIntegerProviderV1
  (signature
    (operator metta-equation 2)
    (operator gslt:ground-integer-less 1)
    (operator quote 1))
  (equations)
  (rewrites
    (rule broad-less-occurrence
      (head (metta-equation (gslt:ground-integer-less ?query)
                            (quote ?query)))
      (body))))
