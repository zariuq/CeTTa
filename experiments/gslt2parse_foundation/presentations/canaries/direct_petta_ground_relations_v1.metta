(gslt-presentation-v1 DirectPeTTaGroundRelationsCanaryV1
  (signature
    (operator DirectGroundDifferentV1 2)
    (operator DirectGroundDifferentTwiceV1 3)
    (operator different 2))
  (equations)
  (rewrites
    (rule direct-ground-different-v1
      (head (DirectGroundDifferentV1 ?left ?right))
      (body
        (different ?left ?right)))
    (rule direct-ground-different-twice-v1
      (head (DirectGroundDifferentTwiceV1 ?first ?second ?third))
      (body
        (different ?first ?second)
        (different ?first ?third)))))
