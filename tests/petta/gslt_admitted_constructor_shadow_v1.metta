; A constructor of admitted syntax cannot simultaneously become a target
; function in the selected domain-transport contract.
(gslt-presentation-v1 AdmittedConstructorShadowV1
  (signature
    (operator metta-equation 2)
    (operator bnf-v1:scalars-cons 2)
    (operator quote 1))
  (equations)
  (rewrites
    (rule replace-admitted-constructor
      (head (metta-equation
        (bnf-v1:scalars-cons ?head ?tail)
        (quote changed-constructor)))
      (body))))
