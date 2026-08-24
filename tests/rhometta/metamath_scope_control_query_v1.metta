!(gslt-rho:query:StateStepV1
   QueryMetamathPushScopeV1
   (StateStepV1
     MetamathPackageV1
     (StateV1 MetamathPackageV1 NatZeroV1 NatZeroV1 TablesNilV1)
     MetamathConstantsGoodOccurrenceV1
     StatePushScopeV1
     $outcome $after $history))

!(gslt-rho:query:StateStepV1
   QueryMetamathPopScopeUnderflowV1
   (StateStepV1
     MetamathPackageV1
     (StateV1 MetamathPackageV1 NatZeroV1 NatZeroV1 TablesNilV1)
     MetamathConstantsRepeatedOccurrenceV1
     StatePopScopeV1
     $outcome $after $history))

!(gslt-rho:query:StateStepV1
   QueryMetamathPopScopeV1
   (StateStepV1
     MetamathPackageV1
     (StateV1 MetamathPackageV1
       (NatSuccV1 NatZeroV1) NatZeroV1 TablesNilV1)
     MetamathConstantsGoodOccurrenceV1
     StatePopScopeV1
     $outcome $after $history))

!(gslt-rho:query:StateStepV1
   QueryMetamathNoopV1
   (StateStepV1
     MetamathPackageV1
     (StateV1 MetamathPackageV1 NatZeroV1 NatZeroV1 TablesNilV1)
     MetamathConstantsGoodOccurrenceV1
     StateNoopV1
     $outcome $after $history))
