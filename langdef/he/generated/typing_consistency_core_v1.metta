(gslt-presentation-v1 he-typing-consistency-core
  (nik-authority-frame-v1
    (mode direct-decision)
    (certificate-policy none)
    (fiber he)
    (outcome-algebra (HCheckEstablished HCheckRefuted HCheckUndetermined HCheckIncomplete)
      (exclusive)
      (default HCheckUndetermined))
    (native-projection pending)
    (status AUTHORED_FRAGMENT)
    (commitments direct-computation certificate-free exclusive-outcomes explicit-default directed-consistency atom-is-top))
  (signature
    (operator HTList 1)
    (operator HTArrow 2)
    (operator HEType 1)
    (operator HENonDynamic 1)
    (operator HEOrdinaryRoot 1)
    (operator HEConsistent 3)
  )
  (equations)
  (rewrites
    (rule type-number
      (nik-rule-frame-v1
        (native-projection native-type-formation)
        (role calculus)) (head (HEType HTNumber)) (body))
    (rule type-string
      (nik-rule-frame-v1
        (native-projection native-type-formation)
        (role calculus)) (head (HEType HTString)) (body))
    (rule type-bool
      (nik-rule-frame-v1
        (native-projection native-type-formation)
        (role calculus)) (head (HEType HTBool)) (body))
    (rule type-dynamic
      (nik-rule-frame-v1
        (native-projection native-type-formation)
        (role calculus)) (head (HEType HTDynamic)) (body))
    (rule type-atom
      (nik-rule-frame-v1
        (native-projection native-type-formation)
        (role calculus)) (head (HEType HTAtom)) (body))
    (rule type-list
      (nik-rule-frame-v1
        (native-projection native-type-formation)
        (role calculus)) (head (HEType (HTList ?t))) (body (HEType ?t)))
    (rule type-arrow
      (nik-rule-frame-v1
        (native-projection native-type-formation)
        (role calculus)) (head (HEType (HTArrow ?a ?r))) (body (HEType ?a) (HEType ?r)))
    (rule non-dynamic-number
      (nik-rule-frame-v1
        (native-projection native-nondynamic-classification)
        (role calculus)) (head (HENonDynamic HTNumber)) (body))
    (rule non-dynamic-string
      (nik-rule-frame-v1
        (native-projection native-nondynamic-classification)
        (role calculus)) (head (HENonDynamic HTString)) (body))
    (rule non-dynamic-bool
      (nik-rule-frame-v1
        (native-projection native-nondynamic-classification)
        (role calculus)) (head (HENonDynamic HTBool)) (body))
    (rule non-dynamic-atom
      (nik-rule-frame-v1
        (native-projection native-nondynamic-classification)
        (role calculus)) (head (HENonDynamic HTAtom)) (body))
    (rule non-dynamic-list
      (nik-rule-frame-v1
        (native-projection native-nondynamic-classification)
        (role calculus)) (head (HENonDynamic (HTList ?t))) (body (HEType ?t)))
    (rule non-dynamic-arrow
      (nik-rule-frame-v1
        (native-projection native-nondynamic-classification)
        (role calculus)) (head (HENonDynamic (HTArrow ?a ?r))) (body (HEType ?a) (HEType ?r)))
    (rule ordinary-number
      (nik-rule-frame-v1
        (native-projection native-ordinary-root-classification)
        (role calculus)) (head (HEOrdinaryRoot HTNumber)) (body))
    (rule ordinary-string
      (nik-rule-frame-v1
        (native-projection native-ordinary-root-classification)
        (role calculus)) (head (HEOrdinaryRoot HTString)) (body))
    (rule ordinary-bool
      (nik-rule-frame-v1
        (native-projection native-ordinary-root-classification)
        (role calculus)) (head (HEOrdinaryRoot HTBool)) (body))
    (rule ordinary-list
      (nik-rule-frame-v1
        (native-projection native-ordinary-root-classification)
        (role calculus)) (head (HEOrdinaryRoot (HTList ?t))) (body (HEType ?t)))
    (rule ordinary-arrow
      (nik-rule-frame-v1
        (native-projection native-ordinary-root-classification)
        (role calculus)) (head (HEOrdinaryRoot (HTArrow ?a ?r))) (body (HEType ?a) (HEType ?r)))
    (rule consistent-exact
      (nik-rule-frame-v1
        (native-projection native-directed-consistency)
        (role calculus)) (head (HEConsistent ?t ?t HEdgeExact)) (body (HEOrdinaryRoot ?t)))
    (rule consistent-dynamic-left
      (nik-rule-frame-v1
        (native-projection native-directed-consistency)
        (role calculus)) (head (HEConsistent HTDynamic ?t HEdgeDynamic)) (body (HEType ?t)))
    (rule consistent-dynamic-right
      (nik-rule-frame-v1
        (native-projection native-directed-consistency)
        (role calculus)) (head (HEConsistent ?t HTDynamic HEdgeDynamic)) (body (HEType ?t)))
    (rule consistent-top-right
      (nik-rule-frame-v1
        (native-projection native-directed-consistency)
        (role calculus)) (head (HEConsistent ?t HTAtom HEdgeTop)) (body (HENonDynamic ?t)))
  ))
