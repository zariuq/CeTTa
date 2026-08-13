(gslt-presentation-v1 prime-native-ground-judgments-v1
  (nik-authority-frame-v1
    (mode direct-decision)
    (certificate-policy none)
    (fiber prime)
    (outcome-algebra (PEstablished PRefuted PUndetermined PIncomplete)
      (exclusive)
      (default PUndetermined))
    (native-projection pending)
    (status AUTHORED_FRAGMENT)
    (commitments direct-computation certificate-free exclusive-outcomes explicit-default directed-consistency atom-is-top calibration-tagged))
  (signature
    (operator PList 1)
    (operator PArrow 2)
    (operator PAdd 2)
    (operator PForm 1)
    (operator PSynth 2)
    (operator PCheck 2)
    (operator PAnalyze 2)
    (operator PConvert 2)
    (operator PRefine 1)
    (operator PrimeJudges 2)
    (operator PrimeClosedType 1)
    (operator PrimeNotType 1)
    (operator PrimeIntrinsic 2)
    (operator PrimeConsistentDir 3)
    (operator PrimeGradualEdge 1)
    (operator PrimeDisjoint 2)
    (operator PrimeNormalizes 2)
  )
  (equations)
  (rewrites
    (rule prime-native-ground-closed-type
      (nik-rule-frame-v1
        (native-projection native-formation)
        (role calculus))
      (head (PrimeClosedType PType))
      (body))
    (rule prime-native-ground-closed-dynamic
      (nik-rule-frame-v1
        (native-projection native-formation)
        (role calculus))
      (head (PrimeClosedType PDynamic))
      (body))
    (rule prime-native-ground-closed-atom
      (nik-rule-frame-v1
        (native-projection native-formation)
        (role calculus))
      (head (PrimeClosedType PAtom))
      (body))
    (rule prime-native-ground-closed-number
      (nik-rule-frame-v1
        (native-projection native-formation)
        (role calculus))
      (head (PrimeClosedType PNumber))
      (body))
    (rule prime-native-ground-closed-string
      (nik-rule-frame-v1
        (native-projection native-formation)
        (role calculus))
      (head (PrimeClosedType PString))
      (body))
    (rule prime-native-ground-closed-bool
      (nik-rule-frame-v1
        (native-projection native-formation)
        (role calculus))
      (head (PrimeClosedType PBool))
      (body))
    (rule prime-native-ground-closed-list
      (nik-rule-frame-v1
        (native-projection native-formation)
        (role calculus))
      (head (PrimeClosedType (PList ?type)))
      (body (PrimeClosedType ?type)))
    (rule prime-native-ground-closed-arrow
      (nik-rule-frame-v1
        (native-projection native-formation)
        (role calculus))
      (head (PrimeClosedType (PArrow ?domain ?codomain)))
      (body (PrimeClosedType ?domain) (PrimeClosedType ?codomain)))
    (rule prime-native-ground-value-number-not-type
      (nik-rule-frame-v1
        (native-projection native-formation-refutation)
        (role calculus))
      (head (PrimeNotType PVNumber))
      (body))
    (rule prime-native-ground-value-string-not-type
      (nik-rule-frame-v1
        (native-projection native-formation-refutation)
        (role calculus))
      (head (PrimeNotType PVString))
      (body))
    (rule prime-native-ground-value-true-not-type
      (nik-rule-frame-v1
        (native-projection native-formation-refutation)
        (role calculus))
      (head (PrimeNotType PVTrue))
      (body))
    (rule prime-native-ground-value-false-not-type
      (nik-rule-frame-v1
        (native-projection native-formation-refutation)
        (role calculus))
      (head (PrimeNotType PVFalse))
      (body))
    (rule prime-native-ground-intrinsic-number
      (nik-rule-frame-v1
        (native-projection native-synthesis)
        (role calculus))
      (head (PrimeIntrinsic PVNumber PNumber))
      (body))
    (rule prime-native-ground-intrinsic-string
      (nik-rule-frame-v1
        (native-projection native-synthesis)
        (role calculus))
      (head (PrimeIntrinsic PVString PString))
      (body))
    (rule prime-native-ground-intrinsic-true
      (nik-rule-frame-v1
        (native-projection native-synthesis)
        (role calculus))
      (head (PrimeIntrinsic PVTrue PBool))
      (body))
    (rule prime-native-ground-intrinsic-false
      (nik-rule-frame-v1
        (native-projection native-synthesis)
        (role calculus))
      (head (PrimeIntrinsic PVFalse PBool))
      (body))
    (rule prime-native-ground-consistent-exact
      (nik-rule-frame-v1
        (native-projection native-directed-consistency)
        (role calculus))
      (head (PrimeConsistentDir ?type ?type PEdgeExact))
      (body (PrimeClosedType ?type)))
    (rule prime-native-ground-consistent-dynamic-right
      (nik-rule-frame-v1
        (native-projection native-directed-consistency)
        (role calculus))
      (head (PrimeConsistentDir ?type PDynamic PEdgeDynamic))
      (body (PrimeClosedType ?type)))
    (rule prime-native-ground-consistent-top-right
      (nik-rule-frame-v1
        (native-projection native-directed-consistency)
        (role calculus))
      (head (PrimeConsistentDir ?type PAtom PEdgeTop))
      (body (PrimeClosedType ?type)))
    (rule prime-native-ground-gradual-edge-dynamic
      (nik-rule-frame-v1
        (native-projection native-gradual-edge)
        (role calculus))
      (head (PrimeGradualEdge PEdgeDynamic))
      (body))
    (rule prime-native-ground-gradual-edge-top
      (nik-rule-frame-v1
        (native-projection native-gradual-edge)
        (role calculus))
      (head (PrimeGradualEdge PEdgeTop))
      (body))
    (rule prime-native-ground-disjoint-number-string
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus))
      (head (PrimeDisjoint PNumber PString))
      (body))
    (rule prime-native-ground-disjoint-number-bool
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus))
      (head (PrimeDisjoint PNumber PBool))
      (body))
    (rule prime-native-ground-disjoint-string-number
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus))
      (head (PrimeDisjoint PString PNumber))
      (body))
    (rule prime-native-ground-disjoint-string-bool
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus))
      (head (PrimeDisjoint PString PBool))
      (body))
    (rule prime-native-ground-disjoint-bool-number
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus))
      (head (PrimeDisjoint PBool PNumber))
      (body))
    (rule prime-native-ground-disjoint-bool-string
      (nik-rule-frame-v1
        (native-projection native-disjointness)
        (role calculus))
      (head (PrimeDisjoint PBool PString))
      (body))
    (rule prime-native-ground-normalize-closed
      (nik-rule-frame-v1
        (native-projection native-conversion)
        (role calculus))
      (head (PrimeNormalizes ?type ?type))
      (body (PrimeClosedType ?type)))
    (rule prime-native-ground-normalize-value-number
      (nik-rule-frame-v1
        (native-projection native-conversion)
        (role calculus))
      (head (PrimeNormalizes PVNumber PVNumber))
      (body))
    (rule prime-native-ground-normalize-value-string
      (nik-rule-frame-v1
        (native-projection native-conversion)
        (role calculus))
      (head (PrimeNormalizes PVString PVString))
      (body))
    (rule prime-native-ground-normalize-one-plus-one
      (nik-rule-frame-v1
        (native-projection native-conversion)
        (role calibration))
      (head (PrimeNormalizes (PAdd POne POne) PTwo))
      (body))
    (rule prime-native-ground-normalize-two
      (nik-rule-frame-v1
        (native-projection native-conversion)
        (role calibration))
      (head (PrimeNormalizes PTwo PTwo))
      (body))
    (rule prime-native-ground-form-established
      (nik-rule-frame-v1
        (native-projection native-formation)
        (role calculus))
      (head (PrimeJudges (PForm ?type) PEstablished))
      (body (PrimeClosedType ?type)))
    (rule prime-native-ground-form-refuted-value
      (nik-rule-frame-v1
        (native-projection native-formation)
        (role calculus))
      (head (PrimeJudges (PForm ?value) PRefuted))
      (body (PrimeNotType ?value)))
    (rule prime-native-ground-synth-intrinsic
      (nik-rule-frame-v1
        (native-projection native-synthesis)
        (role calculus))
      (head (PrimeJudges (PSynth ?value ?type) PEstablished))
      (body (PrimeIntrinsic ?value ?type)))
    (rule prime-native-ground-check-intrinsic-exact
      (nik-rule-frame-v1
        (native-projection native-checking)
        (role calculus))
      (head (PrimeJudges (PCheck ?value ?expected) PEstablished))
      (body
        (PrimeIntrinsic ?value ?actual)
        (PrimeConsistentDir ?actual ?expected PEdgeExact)))
    (rule prime-native-ground-check-intrinsic-refuted
      (nik-rule-frame-v1
        (native-projection native-checking)
        (role calculus))
      (head (PrimeJudges (PCheck ?value ?expected) PRefuted))
      (body
        (PrimeIntrinsic ?value ?actual)
        (PrimeDisjoint ?actual ?expected)))
    (rule prime-native-ground-check-intrinsic-gradual
      (nik-rule-frame-v1
        (native-projection native-checking)
        (role calculus))
      (head (PrimeJudges (PCheck ?value ?expected) PUndetermined))
      (body
        (PrimeIntrinsic ?value ?actual)
        (PrimeConsistentDir ?actual ?expected ?edge)
        (PrimeGradualEdge ?edge)))
    (rule prime-native-ground-analyze-intrinsic
      (nik-rule-frame-v1
        (native-projection native-analysis)
        (role calculus))
      (head (PrimeJudges (PAnalyze ?value ?expected) PEstablished))
      (body
        (PrimeIntrinsic ?value ?actual)
        (PrimeConsistentDir ?actual ?expected ?edge)))
    (rule prime-native-ground-analyze-intrinsic-refuted
      (nik-rule-frame-v1
        (native-projection native-analysis)
        (role calculus))
      (head (PrimeJudges (PAnalyze ?value ?expected) PRefuted))
      (body
        (PrimeIntrinsic ?value ?actual)
        (PrimeDisjoint ?actual ?expected)))
    (rule prime-native-ground-convert-equal-normal-forms
      (nik-rule-frame-v1
        (native-projection native-conversion)
        (role calculus))
      (head (PrimeJudges (PConvert ?left ?right) PEstablished))
      (body
        (PrimeNormalizes ?left ?normal)
        (PrimeNormalizes ?right ?normal)))
    (rule prime-native-ground-convert-distinct-normal-forms
      (nik-rule-frame-v1
        (native-projection native-conversion)
        (role calculus))
      (head (PrimeJudges (PConvert ?left ?right) PRefuted))
      (body
        (PrimeNormalizes ?left ?leftNormal)
        (PrimeNormalizes ?right ?rightNormal)
        (PrimeDisjoint ?leftNormal ?rightNormal)))
    (rule prime-native-ground-refine-unmarked-closed
      (nik-rule-frame-v1
        (native-projection native-refinement)
        (role calculus))
      (head (PrimeJudges (PRefine ?type) PEstablished))
      (body (PrimeClosedType ?type)))
    (rule prime-native-ground-refine-refuted-value
      (nik-rule-frame-v1
        (native-projection native-refinement)
        (role calculus))
      (head (PrimeJudges (PRefine ?value) PRefuted))
      (body (PrimeNotType ?value)))
  ))
