; he-prime declared-constructor type-formation checks.
(: Fact Type)
(: fact Fact)
(: WitnessOf (-> Fact Type))

; Positive: declared constructor receives an argument with the declared domain.
!(assertEqual
  (get-type (WitnessOf fact))
  Type)
(: goodWitness (WitnessOf fact))
!(assertEqual
  (get-type goodWitness)
  (WitnessOf fact))

; Negative: declared constructor rejects arguments outside its domain.
!(assertEqualToResult
  (get-type (WitnessOf 42))
  ())
(: badWitness (WitnessOf 42))
!(assertEqualToResult
  (get-type badWitness)
  ())

; Positive/negative with a dependent binder in the type-constructor domain.
(: Family (-> (: $p Fact) Type))
!(assertEqual
  (get-type (Family fact))
  Type)
!(assertEqualToResult
  (get-type (Family 42))
  ())
(: goodFamilyWitness (Family fact))
(: badFamilyWitness (Family 42))
!(assertEqual
  (get-type goodFamilyWitness)
  (Family fact))
!(assertEqualToResult
  (get-type badFamilyWitness)
  ())

; Open divergence case: Number-indexed families still admit negative indices.
; Rejecting this needs a Nat/refinement discipline, not mere formation checking.
(: VecN (-> $t Number Type))
!(assertEqual
  (get-type (VecN String -1))
  Type)

; Revision-keyed cache safety: a cached rejection must not survive mutation.
(: CachedFact Type)
(: cachedFact OtherFact)
(: CachedWitness (-> CachedFact Type))
!(assertEqualToResult
  (get-type (CachedWitness cachedFact))
  ())
!(assertEqual
  (add-atom &self (: cachedFact CachedFact))
  ())
!(assertEqual
  (get-type (CachedWitness cachedFact))
  Type)
