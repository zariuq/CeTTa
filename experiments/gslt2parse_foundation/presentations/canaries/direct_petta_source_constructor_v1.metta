(gslt-presentation-v1 DirectPeTTaSourceConstructorV1
  (signature
    (operator cons 2)
    (operator DirectSourceConsHeadV1 2))
  (equations)
  (rewrites
    (rule direct-source-cons-head-v1
      (head
        (DirectSourceConsHeadV1
          (cons ?head ?tail)
          ?head))
      (body))))
