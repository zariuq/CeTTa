!(gslt-rho:query:StateStepV1
   QueryMetamathRequireDepthAcceptedV1
   (StateStepV1
     MetamathPackageV1
     (StateV1 MetamathPackageV1 NatZeroV1 NatZeroV1 TablesNilV1)
     MetamathConstantsGoodOccurrenceV1
     (StateRequireDepthV1 NatZeroV1)
     $outcome $after $history))

!(gslt-rho:query:StateStepV1
   QueryMetamathRequireDepthRefusedV1
   (StateStepV1
     MetamathPackageV1
     (StateV1 MetamathPackageV1
       (NatSuccV1 NatZeroV1) NatZeroV1 TablesNilV1)
     MetamathConstantsRepeatedOccurrenceV1
     (StateRequireDepthV1 NatZeroV1)
     $outcome $after $history))

!(gslt-rho:query:StateStepV1
   QueryMetamathRequireDepthMissingOccurrenceV1
   (StateStepV1
     MetamathPackageV1
     (StateV1 MetamathPackageV1 NatZeroV1 NatZeroV1 TablesNilV1)
     MetamathConstantsMissingOccurrenceV1
     (StateRequireDepthV1 NatZeroV1)
     $outcome $after $history))
