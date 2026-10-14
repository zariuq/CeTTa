; A program-authored lowering from two binding constructors to Prime's
; ordinary lambda and let forms. The ABT signature supplies their scopes;
; these equations supply their execution meaning. Calls use the existing
; deterministic equation LanguageDef interpreter and return syntax as data.
(gslt-presentation-v1 DeclaredBindingLoweringV1
  (signature
    (operator metta-equation 2)
    (operator binding:lower 1)
    (operator binding:lambda 2)
    (operator binding:where 3)
    (operator binding:result 1)
    (operator lam 2)
    (operator let 3)
    (operator langdef:list->expression 1)
    (operator LCons 2)
  )
  (equations)
  (rewrites
    (rule binding-lower-lambda
      (head (metta-equation
        (binding:lower (binding:lambda ?parameter ?body))
        (langdef:list->expression
          (LCons lam (LCons ?parameter
            (LCons (binding:lower ?body) LNil))))))
      (body))
    (rule binding-lower-where
      (head (metta-equation
        (binding:lower (binding:where ?pattern ?value ?body))
        (langdef:list->expression
          (LCons let (LCons ?pattern (LCons ?value
            (LCons (binding:lower ?body) LNil)))))))
      (body))
    (rule binding-lower-result
      (head (metta-equation
        (binding:lower (binding:result ?body)) ?body))
      (body))
  )
)
