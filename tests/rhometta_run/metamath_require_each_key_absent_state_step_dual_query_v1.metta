(rhometta:run-values
  (gslt-rho:process:StateStepV1
    QueryMetamathRequireEachKeyAbsentAcceptedRhoCliV1
    (StateStepV1
      MetamathPackageV1
      (StateV1
        MetamathPackageV1 NatZeroV1 NatZeroV1
        (TablesConsV1
          (TableV1 MetamathRowsTableV1 EntriesNilV1)
          TablesNilV1))
      MetamathConstantsGoodOccurrenceV1
      (StateRequireEachKeyAbsentV1
        mm-symbol MetamathRowsTableV1
        (NatSuccV1 NatZeroV1)
        (StateRowConsV1 StateInputV1 StateRowNilV1))
      $outcome $after $history)))

(rhometta:run-values
  (gslt-rho:process:StateStepV1
    QueryMetamathRequireEachKeyAbsentRefusedRhoCliV1
    (StateStepV1
      MetamathPackageV1
      (StateV1
        MetamathPackageV1 NatZeroV1 (NatSuccV1 NatZeroV1)
        (TablesConsV1
          (TableV1
            MetamathRowsTableV1
            (EntriesConsV1
              (EntryV1 NatZeroV1
                (RowConsV1 (NatSuccV1 NatZeroV1) RowNilV1)
                NatZeroV1)
              EntriesNilV1))
          TablesNilV1))
      MetamathConstantsGoodOccurrenceV1
      (StateRequireEachKeyAbsentV1
        mm-symbol MetamathRowsTableV1
        (NatSuccV1 NatZeroV1)
        (StateRowConsV1 StateInputV1 StateRowNilV1))
      $outcome $after $history)))
