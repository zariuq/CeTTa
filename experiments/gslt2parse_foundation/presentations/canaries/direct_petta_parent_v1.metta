(gslt-presentation-v1 DirectPeTTaParentV1
  (signature
    (operator DirectParentV1 2))
  (equations)
  (rewrites
    (rule direct-parent-alice-bob-v1
      (head (DirectParentV1 DirectAliceV1 DirectBobV1))
      (body))
    (rule direct-parent-bob-cara-v1
      (head (DirectParentV1 DirectBobV1 DirectCaraV1))
      (body))
    (rule direct-parent-bob-dara-v1
      (head (DirectParentV1 DirectBobV1 DirectDaraV1))
      (body))))
