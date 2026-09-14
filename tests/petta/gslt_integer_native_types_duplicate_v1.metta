(gslt-presentation-v1 DuplicateIntegerProviderV1
  (signature
    (operator metta-equation 2)
    (operator gslt:ground-integer-less 1)
    (operator ground-integer-less 2)
    (operator if 3)
    (operator < 2)
    (operator quote 1)
    (operator metta-nullary 1))
  (equations)
  (rewrites
    (rule duplicate-less-occurrence
      (head (metta-equation
        (gslt:ground-integer-less (ground-integer-less ?a ?b))
        (if (< ?a ?b)
          (quote (ground-integer-less ?a ?b))
          (metta-nullary empty))))
      (body))))
