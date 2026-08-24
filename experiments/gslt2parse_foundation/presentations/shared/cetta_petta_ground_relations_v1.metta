(gslt-presentation-v1 CettaPeTTaGroundRelationsV1
  (signature
    (operator metta-equation 2)
    (operator gslt:different 1)
    (operator different 2)
    (operator if 3)
    (operator == 2)
    (operator quote 1)
    (operator metta-nullary 1))
  (equations)
  (rewrites
    (rule petta-ground-different-v1
      (head
        (metta-equation
          (gslt:different (different ?left ?right))
          (if (== (quote ?left) (quote ?right))
            (metta-nullary empty)
            (quote (different ?left ?right)))))
      (body))))
