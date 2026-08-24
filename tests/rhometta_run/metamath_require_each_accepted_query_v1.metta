(rhometta:run-values
  (gslt-rho:process:StateStepV1
    QueryMetamathRequireEachAcceptedRhoCliV1
    (StateStepV1
      MetamathPackageV1
      (StateV1
        MetamathPackageV1 NatZeroV1
        (NatSuccV1 (NatSuccV1 NatZeroV1))
        (TablesConsV1
          (TableV1
            MetamathSymbolsTableV1
            (EntriesConsV1
              (EntryV1 NatZeroV1
                (RowConsV1 NatZeroV1 RowNilV1) NatZeroV1)
              (EntriesConsV1
                (EntryV1 (NatSuccV1 NatZeroV1)
                  (RowConsV1 (NatSuccV1 NatZeroV1) RowNilV1)
                  NatZeroV1)
                EntriesNilV1)))
          TablesNilV1))
      MetamathConstantsGoodOccurrenceV1
      (StateRequireEachV1
        mm-symbol MetamathSymbolsTableV1
        (StateRowConsV1 StateInputV1 StateRowNilV1))
      $outcome $after $history)))
