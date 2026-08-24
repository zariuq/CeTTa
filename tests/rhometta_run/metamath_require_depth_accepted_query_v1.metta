(rhometta:run-values
  (gslt-rho:process:StateStepV1
    QueryMetamathRequireDepthAcceptedRhoCliV1
    (StateStepV1
      MetamathPackageV1
      (StateV1 MetamathPackageV1 NatZeroV1 NatZeroV1 TablesNilV1)
      MetamathConstantsGoodOccurrenceV1
      (StateRequireDepthV1 NatZeroV1)
      $outcome $after $history)))

(rhometta:run-values
  (gslt-rho:process:StateStepV1
    QueryMetamathRequireDepthRefusedRhoCliV1
    (StateStepV1
      MetamathPackageV1
      (StateV1 MetamathPackageV1
        (NatSuccV1 NatZeroV1) NatZeroV1 TablesNilV1)
      MetamathConstantsRepeatedOccurrenceV1
      (StateRequireDepthV1 NatZeroV1)
      $outcome $after $history)))
