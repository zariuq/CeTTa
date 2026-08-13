(gslt-presentation-v1 he-typing-closed-ground-core-v1
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
    (operator HEClosedType 1)
    (operator HEIntrinsic 2)
    (operator HEConsistent 3)
    (operator HEDisjoint 2)
    (operator HEPolicyAccepts 2)
    (operator HEPolicyDefers 2)
    (operator HENormalizes 3)
    (operator HERefines 2)
    (operator HEChecks 5)
  )
  (equations)
  (rewrites
    (rule he-closed-ground-closed-number
      (nik-rule-frame-v1
        (native-projection native-type-formation)
        (role calculus)) (head (HEClosedType HTNumber)) (body))
    (rule he-closed-ground-closed-string
      (nik-rule-frame-v1
        (native-projection native-type-formation)
        (role calculus)) (head (HEClosedType HTString)) (body))
    (rule he-closed-ground-closed-bool
      (nik-rule-frame-v1
        (native-projection native-type-formation)
        (role calculus)) (head (HEClosedType HTBool)) (body))
    (rule he-closed-ground-closed-dynamic
      (nik-rule-frame-v1
        (native-projection native-type-formation)
        (role calculus)) (head (HEClosedType HTDynamic)) (body))
    (rule he-closed-ground-closed-atom
      (nik-rule-frame-v1
        (native-projection native-type-formation)
        (role calculus)) (head (HEClosedType HTAtom)) (body))
    (rule he-closed-ground-closed-list
      (nik-rule-frame-v1
        (native-projection native-type-formation)
        (role calculus))
      (head (HEClosedType (HTList ?type)))
      (body (HEClosedType ?type)))
    (rule he-closed-ground-closed-arrow
      (nik-rule-frame-v1
        (native-projection native-type-formation)
        (role calculus))
      (head (HEClosedType (HTArrow ?domain ?codomain)))
      (body (HEClosedType ?domain) (HEClosedType ?codomain)))
    (rule he-closed-ground-intrinsic-number
      (nik-rule-frame-v1
        (native-projection native-synthesis)
        (role calculus))
      (head (HEIntrinsic HVNumber HTNumber))
      (body))
    (rule he-closed-ground-intrinsic-string
      (nik-rule-frame-v1
        (native-projection native-synthesis)
        (role calculus))
      (head (HEIntrinsic HVString HTString))
      (body))
    (rule he-closed-ground-intrinsic-true
      (nik-rule-frame-v1
        (native-projection native-synthesis)
        (role calculus))
      (head (HEIntrinsic HVTrue HTBool))
      (body))
    (rule he-closed-ground-intrinsic-false
      (nik-rule-frame-v1
        (native-projection native-synthesis)
        (role calculus))
      (head (HEIntrinsic HVFalse HTBool))
      (body))
    (rule he-closed-ground-consistent-exact
      (nik-rule-frame-v1
        (native-projection native-directed-consistency)
        (role calculus))
      (head (HEConsistent ?type ?type HEdgeExact))
      (body (HEClosedType ?type)))
    (rule he-closed-ground-consistent-dynamic-left
      (nik-rule-frame-v1
        (native-projection native-directed-consistency)
        (role calculus))
      (head (HEConsistent HTDynamic ?type HEdgeDynamic))
      (body (HEClosedType ?type)))
    (rule he-closed-ground-consistent-dynamic-right
      (nik-rule-frame-v1
        (native-projection native-directed-consistency)
        (role calculus))
      (head (HEConsistent ?type HTDynamic HEdgeDynamic))
      (body (HEClosedType ?type)))
    (rule he-closed-ground-consistent-top-right
      (nik-rule-frame-v1
        (native-projection native-directed-consistency)
        (role calculus))
      (head (HEConsistent ?type HTAtom HEdgeTop))
      (body (HEClosedType ?type)))
    (rule he-closed-ground-disjoint-number-string
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus))
      (head (HEDisjoint HTNumber HTString))
      (body))
    (rule he-closed-ground-disjoint-number-bool
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus))
      (head (HEDisjoint HTNumber HTBool))
      (body))
    (rule he-closed-ground-disjoint-string-number
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus))
      (head (HEDisjoint HTString HTNumber))
      (body))
    (rule he-closed-ground-disjoint-string-bool
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus))
      (head (HEDisjoint HTString HTBool))
      (body))
    (rule he-closed-ground-disjoint-bool-number
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus))
      (head (HEDisjoint HTBool HTNumber))
      (body))
    (rule he-closed-ground-disjoint-bool-string
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus))
      (head (HEDisjoint HTBool HTString))
      (body))
    (rule he-closed-ground-permissive-accepts-exact
      (nik-rule-frame-v1
        (native-projection native-policy-classification)
        (role calculus))
      (head (HEPolicyAccepts HEPermissive HEdgeExact))
      (body))
    (rule he-closed-ground-permissive-accepts-dynamic
      (nik-rule-frame-v1
        (native-projection native-policy-classification)
        (role calculus))
      (head (HEPolicyAccepts HEPermissive HEdgeDynamic))
      (body))
    (rule he-closed-ground-permissive-accepts-top
      (nik-rule-frame-v1
        (native-projection native-policy-classification)
        (role calculus))
      (head (HEPolicyAccepts HEPermissive HEdgeTop))
      (body))
    (rule he-closed-ground-structural-only-accepts-exact
      (nik-rule-frame-v1
        (native-projection native-policy-classification)
        (role calculus))
      (head (HEPolicyAccepts HEStructuralOnly HEdgeExact))
      (body))
    (rule he-closed-ground-structural-only-defers-dynamic
      (nik-rule-frame-v1
        (native-projection native-policy-classification)
        (role calculus))
      (head (HEPolicyDefers HEStructuralOnly HEdgeDynamic))
      (body))
    (rule he-closed-ground-structural-only-defers-top
      (nik-rule-frame-v1
        (native-projection native-policy-classification)
        (role calculus))
      (head (HEPolicyDefers HEStructuralOnly HEdgeTop))
      (body))
    (rule he-closed-ground-normalize-pure-closed
      (nik-rule-frame-v1
        (native-projection native-conversion)
        (role calculus))
      (head (HENormalizes ?type ?type HNormalizeComplete))
      (body (HEClosedType ?type)))
    (rule he-closed-ground-refine-unmarked-closed
      (nik-rule-frame-v1
        (native-projection native-refinement)
        (role calculus))
      (head (HERefines ?type HRefinementValid))
      (body (HEClosedType ?type)))
    (rule he-closed-ground-check-intrinsic-established
      (nik-rule-frame-v1
        (native-projection native-checking)
        (role calculus))
      (head (HEChecks ?value ?expected ?policy HCheckEstablished ?edge))
      (body
        (HEIntrinsic ?value ?actual)
        (HEConsistent ?actual ?expected ?edge)
        (HEPolicyAccepts ?policy ?edge)))
    (rule he-closed-ground-check-intrinsic-refuted
      (nik-rule-frame-v1
        (native-projection native-checking)
        (role calculus))
      (head (HEChecks ?value ?expected ?policy HCheckRefuted HEdgeNone))
      (body
        (HEIntrinsic ?value ?actual)
        (HEDisjoint ?actual ?expected)))
    (rule he-closed-ground-check-intrinsic-policy-undetermined
      (nik-rule-frame-v1
        (native-projection native-checking)
        (role calculus))
      (head (HEChecks ?value ?expected ?policy HCheckUndetermined ?edge))
      (body
        (HEIntrinsic ?value ?actual)
        (HEConsistent ?actual ?expected ?edge)
        (HEPolicyDefers ?policy ?edge)))
  ))
