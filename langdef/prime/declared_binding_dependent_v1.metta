; The single-name declared binding fragment in scope-resolved dependent
; syntax. The declared scope reader has already replaced bound references
; by positions. Anonymous lambda parameters carry those positions without
; turning private identities into printed lexical names.
(gslt-presentation-v1 DeclaredBindingDependentV1
  (signature
    (operator metta-equation 2)
    (operator binding:dependent-lower 1)
    (operator binding:lambda 2)
    (operator binding:where 3)
    (operator binding:result 1)
    (operator idx 1)
    (operator lam 2)
    (operator langdef:list->expression 1)
    (operator LCons 2)
  )
  (equations)
  (rewrites
    (rule dependent-lambda
      (head (metta-equation
        (binding:dependent-lower (binding:lambda (idx 0) ?body))
        (langdef:list->expression
          (LCons lam (LCons _
            (LCons (binding:dependent-lower ?body) LNil))))))
      (body))
    (rule dependent-where
      (head (metta-equation
        (binding:dependent-lower (binding:where (idx 0) ?value ?body))
        (langdef:list->expression
          (LCons
            (langdef:list->expression
              (LCons lam (LCons _
                (LCons (binding:dependent-lower ?body) LNil))))
            (LCons ?value LNil)))))
      (body))
    (rule dependent-result
      (head (metta-equation
        (binding:dependent-lower (binding:result ?body)) ?body))
      (body))
  )
)
