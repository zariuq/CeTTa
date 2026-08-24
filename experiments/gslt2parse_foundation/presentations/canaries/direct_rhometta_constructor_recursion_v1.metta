(gslt-presentation-v1 DirectRhoMettaConstructorRecursionV1
  (signature
    (operator DirectBuildListV1 2)
    (operator NatSuccV1 1)
    (operator DirectBuildConsV1 2))
  (equations)
  (rewrites
    (rule direct-build-list-nil-v1
      (head (DirectBuildListV1 NatZeroV1 DirectBuildNilV1))
      (body))
    (rule direct-build-list-cons-v1
      (head
        (DirectBuildListV1
          (NatSuccV1 ?remaining)
          (DirectBuildConsV1 DirectBuildItemV1 ?tail)))
      (body
        (DirectBuildListV1 ?remaining ?tail)))))
