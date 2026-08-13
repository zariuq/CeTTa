(gslt-presentation-v1 he-profiled-type-inference-core
  (nik-authority-frame-v1
    (mode direct-decision)
    (certificate-policy none)
    (fiber he)
    (outcome-algebra (HCheckEstablished HCheckRefuted HCheckUndetermined HCheckIncomplete)
      (exclusive)
      (default HCheckUndetermined))
    (native-projection pending)
    (status AUTHORED_FRAGMENT)
    (commitments direct-computation certificate-free exclusive-outcomes explicit-default))
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
      (nik-rule-frame-v1
        (native-projection native-type-formation)
        (role calculus)) (head (HEWellFormed HTNumber)) (body))
    (rule well-formed-string
      (nik-rule-frame-v1
        (native-projection native-type-formation)
        (role calculus)) (head (HEWellFormed HTString)) (body))
    (rule well-formed-bool
      (nik-rule-frame-v1
        (native-projection native-type-formation)
        (role calculus)) (head (HEWellFormed HTBool)) (body))
    (rule well-formed-dynamic
      (nik-rule-frame-v1
        (native-projection native-type-formation)
        (role calculus)) (head (HEWellFormed HTDynamic)) (body))
    (rule well-formed-arrow
      (nik-rule-frame-v1
        (native-projection native-type-formation)
        (role calculus))
      (head (HEWellFormed (HTArrow ?domains ?result)))
      (body (HEWellFormedList ?domains) (HEWellFormed ?result)))
    (rule well-formed-list-nil
      (nik-rule-frame-v1
        (native-projection native-type-formation)
        (role calculus))
      (head (HEWellFormedList HNil))
      (body))
    (rule well-formed-list-cons
      (nik-rule-frame-v1
        (native-projection native-type-formation)
        (role calculus))
      (head (HEWellFormedList (HCons ?type ?types)))
      (body (HEWellFormed ?type) (HEWellFormedList ?types)))
    (rule infer-number
      (nik-rule-frame-v1
        (native-projection native-synthesis)
        (role calculus))
      (head (HEInfers HVNumber HTNumber))
      (body))
    (rule infer-string
      (nik-rule-frame-v1
        (native-projection native-synthesis)
        (role calculus))
      (head (HEInfers HVString HTString))
      (body))
    (rule infer-true
      (nik-rule-frame-v1
        (native-projection native-synthesis)
        (role calculus))
      (head (HEInfers HVTrue HTBool))
      (body))
    (rule infer-false
      (nik-rule-frame-v1
        (native-projection native-synthesis)
        (role calculus))
      (head (HEInfers HVFalse HTBool))
      (body))
    (rule infer-declared
      (nik-rule-frame-v1
        (native-projection native-synthesis)
        (role calculus))
      (head (HEInfers ?subject ?type))
      (body
        (HEDeclared ?subject ?type)
        (HEProfileVisible ?type)
        (HEWellFormed ?type)))
    (rule infer-application
      (nik-rule-frame-v1
        (native-projection native-synthesis)
        (role calculus))
      (head (HEInfers (HVApp ?function ?arguments) ?result))
      (body
        (HEInfers ?function (HTArrow ?domains ?result))
        (HEArgumentsInfer ?arguments ?domains)))
    (rule arguments-nil
      (nik-rule-frame-v1
        (native-projection native-synthesis)
        (role calculus))
      (head (HEArgumentsInfer HNil HNil))
      (body))
    (rule arguments-cons
      (nik-rule-frame-v1
        (native-projection native-synthesis)
        (role calculus))
      (head
        (HEArgumentsInfer
          (HCons ?value ?values)
          (HCons ?type ?types)))
      (body
        (HEInfers ?value ?type)
        (HEArgumentsInfer ?values ?types)))
    (rule disjoint-number-string
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus))
      (head (HEDisjoint HTNumber HTString))
      (body))
    (rule disjoint-number-bool
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus))
      (head (HEDisjoint HTNumber HTBool))
      (body))
    (rule disjoint-string-number
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus))
      (head (HEDisjoint HTString HTNumber))
      (body))
    (rule disjoint-string-bool
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus))
      (head (HEDisjoint HTString HTBool))
      (body))
    (rule disjoint-bool-number
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus))
      (head (HEDisjoint HTBool HTNumber))
      (body))
    (rule disjoint-bool-string
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus))
      (head (HEDisjoint HTBool HTString))
      (body))
  ))
