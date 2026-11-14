(gslt-presentation-v1 DirectPeTTaSourceConstructorV1
  (signature
    (operator cons 2)
    (operator metta-nullary 1)
    (operator DirectZeroV1 0)
    (operator DirectHostConstructorV1 1)
    (operator DirectSourceHostNameV1 2)
    (operator DirectSourceNullaryNameV1 2)
    (operator DirectSourceNullaryV1 2)
    (operator DirectFirstV1 2)
    (operator DirectSecondV1 1)
    (operator DirectSourceConsHeadV1 2)
    (operator DirectSourceConsSameV1 2)
    (operator DirectSourceConsScopeV1 2))
  (equations)
  (rewrites
    (rule direct-source-host-name-v1
      (head (DirectSourceHostNameV1 (DirectHostConstructorV1 ?value) ?value))
      (body))
    (rule direct-source-nullary-name-v1
      (head (DirectSourceNullaryNameV1 (metta-nullary ?value) ?value))
      (body))
    (rule direct-source-nullary-v1
      (head (DirectSourceNullaryV1 (DirectZeroV1) ActualZeroV1))
      (body))
    (rule direct-source-cons-head-v1
      (head
        (DirectSourceConsHeadV1
          (cons ?head ?tail)
          ?head))
      (body))
    (rule direct-source-cons-same-v1
      (head (DirectSourceConsSameV1 (cons ?same ?same) ?same))
      (body))
    (rule direct-source-cons-scope-v1
      (head (DirectSourceConsScopeV1 (cons ?__gslt_input_v1_0_0 ?tail) ?__gslt_input_v1_0_0))
      (body))))
