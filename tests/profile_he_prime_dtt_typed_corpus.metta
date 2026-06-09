; he-prime DTT seed corpus.
; Each block is a typed sibling for an existing MeTTa idiom plus its proof intent.

; PROPERTY: a length-indexed vector drop preserves the element type and decrements
; only a nonzero Peano length; dropping an empty vector is rejected by typing.
(: SeedNat Type)
(: SeedZ SeedNat)
(: SeedS (-> SeedNat SeedNat))
(: SeedVec (-> $t SeedNat Type))
(: SeedNil (SeedVec $t SeedZ))
(: SeedCons (-> $t (SeedVec $t $n) (SeedVec $t (SeedS $n))))
(: seedDrop (-> (SeedVec $t (SeedS $n)) (SeedVec $t $n)))
(= (seedDrop (SeedCons $x $xs)) $xs)

!(assertEqual
  (get-type (seedDrop (SeedCons 1 SeedNil)))
  (SeedVec Number SeedZ))
!(assertEqual
  (seedDrop (SeedCons 1 SeedNil))
  SeedNil)
!(assertEqualToResult
  (get-type (seedDrop SeedNil))
  ())

; PROPERTY: proofs-as-programs should carry the proposition they prove; applying
; an implication to the proposition shape instead of a proof object is rejected.
(: SeedEntity Type)
(: SeedSocrates SeedEntity)
(: SeedPlato SeedEntity)
(: SeedHuman (-> SeedEntity Type))
(: SeedMortal (-> SeedEntity Type))
(: seedSocratesHuman (SeedHuman SeedSocrates))
(: seedPlatoHuman (SeedHuman SeedPlato))
(: seedHumansAreMortal (-> (: $h (SeedHuman $x)) (SeedMortal $x)))
(: seedSocratesMortal (SeedMortal SeedSocrates))
(= (seedHumansAreMortal seedSocratesHuman) seedSocratesMortal)

!(assertEqual
  (get-type (seedHumansAreMortal seedSocratesHuman))
  (SeedMortal SeedSocrates))
!(assertEqualToResult
  (get-type (seedHumansAreMortal (SeedHuman SeedSocrates)))
  ())
!(assertEqualToResult
  (seedHumansAreMortal seedSocratesHuman)
  (seedSocratesMortal))

; PROPERTY: a typed query rule can return a proof object whose family index is
; determined by the query argument; failed lookup yields no proof.
(: SeedFrog (-> SeedEntity Type))
(: seedSam SeedEntity)
(: seedSamFrog (SeedFrog seedSam))
(: seedLookupFrog (-> (: $x SeedEntity) (SeedFrog $x)))
(= (seedLookupFrog $x) (match &self (: $proof (SeedFrog $x)) $proof))

!(assertEqual
  (get-type (seedLookupFrog seedSam))
  (SeedFrog seedSam))
!(assertEqualToResult
  (seedLookupFrog seedSam)
  (seedSamFrog))
!(assertEqualToResult
  (seedLookupFrog SeedPlato)
  ())

; PROPERTY: capabilities are ordinary dependent proof objects; a capability for
; one principal is not accepted for another principal.
(: SeedSecret Type)
(: seedDoc SeedSecret)
(: SeedCanRead (-> SeedEntity SeedSecret Type))
(: SeedReadReceipt (-> SeedEntity SeedSecret Type))
(: seedAlice SeedEntity)
(: seedBob SeedEntity)
(: seedAliceDocCap (SeedCanRead seedAlice seedDoc))
(: seedBobDocCap (SeedCanRead seedBob seedDoc))
(: seedRead (-> (: $who SeedEntity)
                (: $doc SeedSecret)
                (: $cap (SeedCanRead $who $doc))
                (SeedReadReceipt $who $doc)))

!(assertEqual
  (get-type (seedRead seedAlice seedDoc seedAliceDocCap))
  (SeedReadReceipt seedAlice seedDoc))
!(assertEqualToResult
  (get-type (seedRead seedAlice seedDoc seedBobDocCap))
  ())

; PROPERTY: resource/fuel proofs should encode nonzero fuel structurally, so the
; zero budget cannot be supplied to a one-step operation.
(: SeedBudget (-> SeedNat Type))
(: seedBudget0 (SeedBudget SeedZ))
(: seedBudget1 (SeedBudget (SeedS SeedZ)))
(: SeedStepOK (-> SeedNat Type))
(: seedStepWithFuel (-> (: $n SeedNat)
                        (: $fuel (SeedBudget (SeedS $n)))
                        (SeedStepOK $n)))

!(assertEqual
  (get-type (seedStepWithFuel SeedZ seedBudget1))
  (SeedStepOK SeedZ))
!(assertEqualToResult
  (get-type (seedStepWithFuel SeedZ seedBudget0))
  ())

; OPEN: grounded Number-indexed families still admit negative indices. This is a
; refinement/Nat design fork, not a solved DTT invariant.
(: SeedVecN (-> $t Number Type))
!(assertEqual
  (get-type (SeedVecN String -1))
  Type)
