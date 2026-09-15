(gslt-presentation-v1 NullaryIntegerWorkerOverrideV1
  (signature
    (operator metta-equation 2)
    (operator metta-nullary 1)
    (operator quote 1))
  (equations)
  (rewrites
    (rule nullary-worker-override
      (head (metta-equation
        (metta-nullary gslt:fn:IntegerLiteralPairV1:0)
        (quote ExtraV1)))
      (body))))
