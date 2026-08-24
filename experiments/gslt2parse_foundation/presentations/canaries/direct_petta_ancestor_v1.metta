(gslt-presentation-v1 DirectPeTTaAncestorV1
  (signature
    (operator DirectParentV1 2)
    (operator DirectAncestorV1 2))
  (equations)
  (rewrites
    (rule direct-ancestor-base-v1
      (head (DirectAncestorV1 ?first ?last))
      (body
        (DirectParentV1 ?first ?last)))
    (rule direct-ancestor-step-v1
      (head (DirectAncestorV1 ?first ?last))
      (body
        (DirectParentV1 ?first ?middle)
        (DirectAncestorV1 ?middle ?last)))))
