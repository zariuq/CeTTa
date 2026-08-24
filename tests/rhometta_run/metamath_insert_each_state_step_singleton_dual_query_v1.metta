(rhometta:run-values
  (gslt-rho:process:StateStepV1
    QueryMetamathInsertEachStateStepSingletonAcceptedRhoCliV1
    (StateStepV1
      MetamathPackageV1
      (StateV1
        MetamathPackageV1 NatZeroV1 NatZeroV1
        (TablesConsV1
          (TableV1 MetamathRowsTableV1 EntriesNilV1)
          TablesNilV1))
      MetamathConstantsSingletonOccurrenceV1
      (StateInsertEachV1
        mm-symbol MetamathRowsTableV1
        (NatSuccV1 NatZeroV1)
        StateRequireKeyAbsentV1
        (StateRowConsV1 StateInputV1 StateRowNilV1))
      $outcome $after $history)))

(rhometta:run-values
  (gslt-rho:process:StateStepV1
    QueryMetamathInsertEachStateStepSingletonRefusedRhoCliV1
    (StateStepV1
      MetamathPackageV1
      (StateV1
        MetamathPackageV1 NatZeroV1 (NatSuccV1 NatZeroV1)
        (TablesConsV1
          (TableV1
            MetamathRowsTableV1
            (EntriesConsV1
              (EntryV1 NatZeroV1
                (RowConsV1 NatZeroV1 RowNilV1) NatZeroV1)
              EntriesNilV1))
          TablesNilV1))
      MetamathConstantsSingletonOccurrenceV1
      (StateInsertEachV1
        mm-symbol MetamathRowsTableV1
        (NatSuccV1 NatZeroV1)
        StateRequireKeyAbsentV1
        (StateRowConsV1 StateInputV1 StateRowNilV1))
      $outcome $after $history)))
