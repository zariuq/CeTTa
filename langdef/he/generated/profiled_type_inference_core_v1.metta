(gslt-presentation-v1 he-profiled-type-inference-core
  (signature
    (operator HCons 2)
    (operator HVApp 2)
    (operator HTArrow 2)
    (operator HEInfers 2)
    (operator HEDeclared 2)
    (operator HEProfileVisible 1)
    (operator HEWellFormed 1)
    (operator HEWellFormedList 1)
    (operator HEArgumentsInfer 2)
    (operator HEDisjoint 2)
  )
  (equations)
  (rewrites
    (rule well-formed-number
       (head (HEWellFormed HTNumber)) (body))
    (rule well-formed-string
       (head (HEWellFormed HTString)) (body))
    (rule well-formed-bool
       (head (HEWellFormed HTBool)) (body))
    (rule well-formed-dynamic
       (head (HEWellFormed HTDynamic)) (body))
    (rule well-formed-arrow
      (head (HEWellFormed (HTArrow ?domains ?result)))
      (body (HEWellFormedList ?domains) (HEWellFormed ?result)))
    (rule well-formed-list-nil
      (head (HEWellFormedList HNil))
      (body))
    (rule well-formed-list-cons
      (head (HEWellFormedList (HCons ?type ?types)))
      (body (HEWellFormed ?type) (HEWellFormedList ?types)))
    (rule infer-number
      (head (HEInfers HVNumber HTNumber))
      (body))
    (rule infer-string
      (head (HEInfers HVString HTString))
      (body))
    (rule infer-true
      (head (HEInfers HVTrue HTBool))
      (body))
    (rule infer-false
      (head (HEInfers HVFalse HTBool))
      (body))
    (rule infer-declared
      (head (HEInfers ?subject ?type))
      (body
        (HEDeclared ?subject ?type)
        (HEProfileVisible ?type)
        (HEWellFormed ?type)))
    (rule infer-application
      (head (HEInfers (HVApp ?function ?arguments) ?result))
      (body
        (HEInfers ?function (HTArrow ?domains ?result))
        (HEArgumentsInfer ?arguments ?domains)))
    (rule arguments-nil
      (head (HEArgumentsInfer HNil HNil))
      (body))
    (rule arguments-cons
      (head
        (HEArgumentsInfer
          (HCons ?value ?values)
          (HCons ?type ?types)))
      (body
        (HEInfers ?value ?type)
        (HEArgumentsInfer ?values ?types)))
    (rule disjoint-number-string
      (head (HEDisjoint HTNumber HTString))
      (body))
    (rule disjoint-number-bool
      (head (HEDisjoint HTNumber HTBool))
      (body))
    (rule disjoint-string-number
      (head (HEDisjoint HTString HTNumber))
      (body))
    (rule disjoint-string-bool
      (head (HEDisjoint HTString HTBool))
      (body))
    (rule disjoint-bool-number
      (head (HEDisjoint HTBool HTNumber))
      (body))
    (rule disjoint-bool-string
      (head (HEDisjoint HTBool HTString))
      (body))
  ))
