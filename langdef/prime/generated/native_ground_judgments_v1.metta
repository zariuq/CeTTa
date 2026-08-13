(gslt-presentation-v1 prime-native-ground-judgments-v1
  (signature
    (operator PList 1)
    (operator PArrow 2)
    (operator PAdd 2)
    (operator PForm 1)
    (operator PSynth 1)
    (operator PCheck 2)
    (operator PAnalyze 2)
    (operator PConvert 2)
    (operator PRefine 1)
    (operator PrimeJudges 2)
    (operator PrimeClosedType 1)
    (operator PrimeNotType 1)
    (operator PrimeIntrinsic 2)
    (operator PrimeConsistent 3)
    (operator PrimeGradualEdge 1)
    (operator PrimeDisjoint 2)
    (operator PrimeNormalizes 2)
  )
  (equations)
  (rewrites
    (rule prime-native-ground-closed-type
      (head (PrimeClosedType PType))
      (body))
    (rule prime-native-ground-closed-dynamic
      (head (PrimeClosedType PDynamic))
      (body))
    (rule prime-native-ground-closed-atom
      (head (PrimeClosedType PAtom))
      (body))
    (rule prime-native-ground-closed-number
      (head (PrimeClosedType PNumber))
      (body))
    (rule prime-native-ground-closed-string
      (head (PrimeClosedType PString))
      (body))
    (rule prime-native-ground-closed-bool
      (head (PrimeClosedType PBool))
      (body))
    (rule prime-native-ground-closed-list
      (head (PrimeClosedType (PList ?type)))
      (body (PrimeClosedType ?type)))
    (rule prime-native-ground-closed-arrow
      (head (PrimeClosedType (PArrow ?domain ?codomain)))
      (body (PrimeClosedType ?domain) (PrimeClosedType ?codomain)))
    (rule prime-native-ground-value-number-not-type
      (head (PrimeNotType PVNumber))
      (body))
    (rule prime-native-ground-value-string-not-type
      (head (PrimeNotType PVString))
      (body))
    (rule prime-native-ground-value-true-not-type
      (head (PrimeNotType PVTrue))
      (body))
    (rule prime-native-ground-value-false-not-type
      (head (PrimeNotType PVFalse))
      (body))
    (rule prime-native-ground-intrinsic-number
      (head (PrimeIntrinsic PVNumber PNumber))
      (body))
    (rule prime-native-ground-intrinsic-string
      (head (PrimeIntrinsic PVString PString))
      (body))
    (rule prime-native-ground-intrinsic-true
      (head (PrimeIntrinsic PVTrue PBool))
      (body))
    (rule prime-native-ground-intrinsic-false
      (head (PrimeIntrinsic PVFalse PBool))
      (body))
    (rule prime-native-ground-consistent-exact
      (head (PrimeConsistent ?type ?type PEdgeExact))
      (body (PrimeClosedType ?type)))
    (rule prime-native-ground-consistent-dynamic-right
      (head (PrimeConsistent ?type PDynamic PEdgeDynamic))
      (body (PrimeClosedType ?type)))
    (rule prime-native-ground-consistent-top-right
      (head (PrimeConsistent ?type PAtom PEdgeTop))
      (body (PrimeClosedType ?type)))
    (rule prime-native-ground-gradual-edge-dynamic
      (head (PrimeGradualEdge PEdgeDynamic))
      (body))
    (rule prime-native-ground-gradual-edge-top
      (head (PrimeGradualEdge PEdgeTop))
      (body))
    (rule prime-native-ground-disjoint-number-string
      (head (PrimeDisjoint PNumber PString))
      (body))
    (rule prime-native-ground-disjoint-number-bool
      (head (PrimeDisjoint PNumber PBool))
      (body))
    (rule prime-native-ground-disjoint-string-number
      (head (PrimeDisjoint PString PNumber))
      (body))
    (rule prime-native-ground-disjoint-string-bool
      (head (PrimeDisjoint PString PBool))
      (body))
    (rule prime-native-ground-disjoint-bool-number
      (head (PrimeDisjoint PBool PNumber))
      (body))
    (rule prime-native-ground-disjoint-bool-string
      (head (PrimeDisjoint PBool PString))
      (body))
    (rule prime-native-ground-normalize-closed
      (head (PrimeNormalizes ?type ?type))
      (body (PrimeClosedType ?type)))
    (rule prime-native-ground-normalize-value-number
      (head (PrimeNormalizes PVNumber PVNumber))
      (body))
    (rule prime-native-ground-normalize-value-string
      (head (PrimeNormalizes PVString PVString))
      (body))
    (rule prime-native-ground-normalize-one-plus-one
      (head (PrimeNormalizes (PAdd POne POne) PTwo))
      (body))
    (rule prime-native-ground-normalize-two
      (head (PrimeNormalizes PTwo PTwo))
      (body))
    (rule prime-native-ground-form-established
      (head (PrimeJudges (PForm ?type) PEstablished))
      (body (PrimeClosedType ?type)))
    (rule prime-native-ground-form-refuted-value
      (head (PrimeJudges (PForm ?value) PRefuted))
      (body (PrimeNotType ?value)))
    (rule prime-native-ground-synth-intrinsic
      (head (PrimeJudges (PSynth ?value) PEstablished))
      (body (PrimeIntrinsic ?value ?type)))
    (rule prime-native-ground-check-intrinsic-exact
      (head (PrimeJudges (PCheck ?value ?expected) PEstablished))
      (body
        (PrimeIntrinsic ?value ?actual)
        (PrimeConsistent ?actual ?expected PEdgeExact)))
    (rule prime-native-ground-check-intrinsic-refuted
      (head (PrimeJudges (PCheck ?value ?expected) PRefuted))
      (body
        (PrimeIntrinsic ?value ?actual)
        (PrimeDisjoint ?actual ?expected)))
    (rule prime-native-ground-check-intrinsic-gradual
      (head (PrimeJudges (PCheck ?value ?expected) PUndetermined))
      (body
        (PrimeIntrinsic ?value ?actual)
        (PrimeConsistent ?actual ?expected ?edge)
        (PrimeGradualEdge ?edge)))
    (rule prime-native-ground-analyze-intrinsic
      (head (PrimeJudges (PAnalyze ?value ?expected) PEstablished))
      (body
        (PrimeIntrinsic ?value ?actual)
        (PrimeConsistent ?actual ?expected ?edge)))
    (rule prime-native-ground-analyze-intrinsic-refuted
      (head (PrimeJudges (PAnalyze ?value ?expected) PRefuted))
      (body
        (PrimeIntrinsic ?value ?actual)
        (PrimeDisjoint ?actual ?expected)))
    (rule prime-native-ground-convert-equal-normal-forms
      (head (PrimeJudges (PConvert ?left ?right) PEstablished))
      (body
        (PrimeNormalizes ?left ?normal)
        (PrimeNormalizes ?right ?normal)))
    (rule prime-native-ground-convert-distinct-normal-forms
      (head (PrimeJudges (PConvert ?left ?right) PRefuted))
      (body
        (PrimeNormalizes ?left ?leftNormal)
        (PrimeNormalizes ?right ?rightNormal)
        (PrimeDisjoint ?leftNormal ?rightNormal)))
    (rule prime-native-ground-refine-unmarked-closed
      (head (PrimeJudges (PRefine ?type) PEstablished))
      (body (PrimeClosedType ?type)))
  ))
