(rhometta:run-values
  (gslt-rho:process:StateStepV1
    QueryMetamathPushScopeRhoCliV1
    (StateStepV1
      MetamathPackageV1
      (StateV1 MetamathPackageV1 NatZeroV1 NatZeroV1 TablesNilV1)
      MetamathConstantsGoodOccurrenceV1
      StatePushScopeV1
      $outcome $after $history)))

(rhometta:run-values
  (gslt-rho:process:StateStepV1
    QueryMetamathPopScopeUnderflowRhoCliV1
    (StateStepV1
      MetamathPackageV1
      (StateV1 MetamathPackageV1 NatZeroV1 NatZeroV1 TablesNilV1)
      MetamathConstantsRepeatedOccurrenceV1
      StatePopScopeV1
      $outcome $after $history)))

(rhometta:run-values
  (gslt-rho:process:StateStepV1
    QueryMetamathPopScopeRhoCliV1
    (StateStepV1
      MetamathPackageV1
      (StateV1 MetamathPackageV1
        (NatSuccV1 NatZeroV1) NatZeroV1 TablesNilV1)
      MetamathConstantsGoodOccurrenceV1
      StatePopScopeV1
      $outcome $after $history)))

(rhometta:run-values
  (gslt-rho:process:StateStepV1
    QueryMetamathNoopRhoCliV1
    (StateStepV1
      MetamathPackageV1
      (StateV1 MetamathPackageV1 NatZeroV1 NatZeroV1 TablesNilV1)
      MetamathConstantsGoodOccurrenceV1
      StateNoopV1
      $outcome $after $history)))
