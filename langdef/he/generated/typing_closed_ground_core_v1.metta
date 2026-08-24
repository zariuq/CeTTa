(gslt-presentation-v1 he-typing-closed-ground-core-v1
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
       (head (HEClosedType HTNumber)) (body))
    (rule he-closed-ground-closed-string
       (head (HEClosedType HTString)) (body))
    (rule he-closed-ground-closed-bool
       (head (HEClosedType HTBool)) (body))
    (rule he-closed-ground-closed-dynamic
       (head (HEClosedType HTDynamic)) (body))
    (rule he-closed-ground-closed-atom
       (head (HEClosedType HTAtom)) (body))
    (rule he-closed-ground-closed-list
      (head (HEClosedType (HTList ?type)))
      (body (HEClosedType ?type)))
    (rule he-closed-ground-closed-arrow
      (head (HEClosedType (HTArrow ?domain ?codomain)))
      (body (HEClosedType ?domain) (HEClosedType ?codomain)))
    (rule he-closed-ground-intrinsic-number
      (head (HEIntrinsic HVNumber HTNumber))
      (body))
    (rule he-closed-ground-intrinsic-string
      (head (HEIntrinsic HVString HTString))
      (body))
    (rule he-closed-ground-intrinsic-true
      (head (HEIntrinsic HVTrue HTBool))
      (body))
    (rule he-closed-ground-intrinsic-false
      (head (HEIntrinsic HVFalse HTBool))
      (body))
    (rule he-closed-ground-consistent-exact
      (head (HEConsistent ?type ?type HEdgeExact))
      (body (HEClosedType ?type)))
    (rule he-closed-ground-consistent-dynamic-left
      (head (HEConsistent HTDynamic ?type HEdgeDynamic))
      (body (HEClosedType ?type)))
    (rule he-closed-ground-consistent-dynamic-right
      (head (HEConsistent ?type HTDynamic HEdgeDynamic))
      (body (HEClosedType ?type)))
    (rule he-closed-ground-consistent-top-right
      (head (HEConsistent ?type HTAtom HEdgeTop))
      (body (HEClosedType ?type)))
    (rule he-closed-ground-disjoint-number-string
      (head (HEDisjoint HTNumber HTString))
      (body))
    (rule he-closed-ground-disjoint-number-bool
      (head (HEDisjoint HTNumber HTBool))
      (body))
    (rule he-closed-ground-disjoint-string-number
      (head (HEDisjoint HTString HTNumber))
      (body))
    (rule he-closed-ground-disjoint-string-bool
      (head (HEDisjoint HTString HTBool))
      (body))
    (rule he-closed-ground-disjoint-bool-number
      (head (HEDisjoint HTBool HTNumber))
      (body))
    (rule he-closed-ground-disjoint-bool-string
      (head (HEDisjoint HTBool HTString))
      (body))
    (rule he-closed-ground-permissive-accepts-exact
      (head (HEPolicyAccepts HEPermissive HEdgeExact))
      (body))
    (rule he-closed-ground-permissive-accepts-dynamic
      (head (HEPolicyAccepts HEPermissive HEdgeDynamic))
      (body))
    (rule he-closed-ground-permissive-accepts-top
      (head (HEPolicyAccepts HEPermissive HEdgeTop))
      (body))
    (rule he-closed-ground-structural-only-accepts-exact
      (head (HEPolicyAccepts HEStructuralOnly HEdgeExact))
      (body))
    (rule he-closed-ground-structural-only-defers-dynamic
      (head (HEPolicyDefers HEStructuralOnly HEdgeDynamic))
      (body))
    (rule he-closed-ground-structural-only-defers-top
      (head (HEPolicyDefers HEStructuralOnly HEdgeTop))
      (body))
    (rule he-closed-ground-normalize-pure-closed
      (head (HENormalizes ?type ?type HNormalizeComplete))
      (body (HEClosedType ?type)))
    (rule he-closed-ground-refine-unmarked-closed
      (head (HERefines ?type HRefinementValid))
      (body (HEClosedType ?type)))
    (rule he-closed-ground-check-intrinsic-established
      (head (HEChecks ?value ?expected ?policy HCheckEstablished ?edge))
      (body
        (HEIntrinsic ?value ?actual)
        (HEConsistent ?actual ?expected ?edge)
        (HEPolicyAccepts ?policy ?edge)))
    (rule he-closed-ground-check-intrinsic-refuted
      (head (HEChecks ?value ?expected ?policy HCheckRefuted HEdgeNone))
      (body
        (HEIntrinsic ?value ?actual)
        (HEDisjoint ?actual ?expected)))
    (rule he-closed-ground-check-intrinsic-policy-undetermined
      (head (HEChecks ?value ?expected ?policy HCheckUndetermined ?edge))
      (body
        (HEIntrinsic ?value ?actual)
        (HEConsistent ?actual ?expected ?edge)
        (HEPolicyDefers ?policy ?edge)))
  ))
