(gslt-presentation-v1 DirectPeTTaIdentityTargetEquationV1
  (signature
    (operator metta-equation 2)
    (operator DirectIdentityTargetV1 1)
    (operator DirectIdentityWitnessV1 1))
  (equations)
  (rewrites
    (rule direct-identity-target-equation-v1
      (head
        (metta-equation
          (DirectIdentityTargetV1 ?value)
          (DirectIdentityTargetV1 ?value)))
      (body))
    (rule direct-identity-witness-v1
      (head
        (DirectIdentityWitnessV1 DirectIdentityValueV1))
      (body))))
