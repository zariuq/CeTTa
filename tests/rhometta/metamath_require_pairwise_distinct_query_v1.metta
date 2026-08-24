!(gslt-rho:query:StateStepV1
   QueryMetamathPairwiseDistinctAcceptedV1
   (StateStepV1
     MetamathPackageV1
     (StateV1 MetamathPackageV1 NatZeroV1 NatZeroV1 TablesNilV1)
     MetamathConstantsGoodOccurrenceV1
     (StateRequirePairwiseDistinctV1 mm-symbol)
     $outcome $after $history))

!(gslt-rho:query:StateValueCompareV1
   QueryMetamathValueCompareDifferentV1
   (StateValueCompareV1 NatZeroV1 (NatSuccV1 NatZeroV1) $comparison))

!(gslt:different (different NatZeroV1 (NatSuccV1 NatZeroV1)))

!(rhometta:values
   (rhometta:run-canonical
     (rho:eval-payload
       (quote
         (let $_
           (gslt:different (different NatZeroV1 (NatSuccV1 NatZeroV1)))
           (rho:val DirectRhoDifferentAcceptedV1))))))

!(rhometta:values
   (rhometta:run-canonical
     (rho:eval-payload
       (quote
         (if (== NatZeroV1 (NatSuccV1 NatZeroV1))
           (rho:par)
           (rho:val DirectRhoIfDifferentAcceptedV1))))))

!(gslt-rho:query:DistinctScanV1
   QueryMetamathDistinctScanGoodV1
   (DistinctScanV1
     (ValuesConsV1 NatZeroV1
       (ValuesConsV1 (NatSuccV1 NatZeroV1) ValuesNilV1))
     NatZeroV1 SeenNilV1 $result))

!(gslt-rho:query:OccurrenceInterningFaithfulV1
   QueryMetamathInterningFaithfulGoodV1
   (OccurrenceInterningFaithfulV1
     MetamathConstantsGoodOccurrenceV1 mm-symbol))

!(gslt-rho:query:StateInterningInjectiveV1
   QueryMetamathStateInterningInjectiveGoodV1
   (StateInterningInjectiveV1
     MetamathConstantsGoodOccurrenceV1 mm-symbol))

!(gslt-rho:query:OccurrenceRoleValuesV1
   QueryMetamathRoleValuesGoodV1
   (OccurrenceRoleValuesV1
     MetamathConstantsGoodOccurrenceV1 mm-symbol $values))

!(gslt-rho:query:OccurrenceRoleSymbolsV1
   QueryMetamathRoleSymbolsGoodV1
   (OccurrenceRoleSymbolsV1
     MetamathConstantsGoodOccurrenceV1 mm-symbol $symbols))

!(gslt-rho:query:InterningInjectiveOnV1
   QueryMetamathInterningFactGoodV1
   (InterningInjectiveOnV1 $symbols $values))

!(gslt-rho:query:StateStepV1
   QueryMetamathPairwiseDistinctRefusedV1
   (StateStepV1
     MetamathPackageV1
     (StateV1 MetamathPackageV1 NatZeroV1 NatZeroV1 TablesNilV1)
     MetamathConstantsRepeatedOccurrenceV1
     (StateRequirePairwiseDistinctV1 mm-symbol)
     $outcome $after $history))

!(gslt-rho:query:StateStepV1
   QueryMetamathPairwiseDistinctMissingOccurrenceV1
   (StateStepV1
     MetamathPackageV1
     (StateV1 MetamathPackageV1 NatZeroV1 NatZeroV1 TablesNilV1)
     MetamathConstantsMissingOccurrenceV1
     (StateRequirePairwiseDistinctV1 mm-symbol)
     $outcome $after $history))
