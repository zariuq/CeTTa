(rhometta:run-values
  (gslt-rho:process:InsertEachV1
    QueryMetamathInsertEachCoreOneAcceptedRhoCliV1
    (InsertEachV1
      (StateV1
        MetamathPackageV1 NatZeroV1 NatZeroV1
        (TablesConsV1
          (TableV1 MetamathRowsTableV1 EntriesNilV1)
          TablesNilV1))
      MetamathConstantsGoodOccurrenceV1
      MetamathRowsTableV1
      (NatSuccV1 NatZeroV1)
      StateRequireKeyAbsentV1
      (StateRowConsV1 StateInputV1 StateRowNilV1)
      (ValuesConsV1 NatZeroV1 ValuesNilV1)
      $outcome $after $history)))

(rhometta:run-values
  (gslt-rho:process:InsertEachV1
    QueryMetamathInsertEachCoreOneRefusedRhoCliV1
    (InsertEachV1
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
      MetamathConstantsGoodOccurrenceV1
      MetamathRowsTableV1
      (NatSuccV1 NatZeroV1)
      StateRequireKeyAbsentV1
      (StateRowConsV1 StateInputV1 StateRowNilV1)
      (ValuesConsV1 NatZeroV1 ValuesNilV1)
      $outcome $after $history)))

(rhometta:run-values
  (gslt-rho:process:InsertEachAfterStepV1
    QueryMetamathInsertEachCoreRollbackRhoCliV1
    (InsertEachAfterStepV1
      OutcomeRefusedV1
      (StateV1
        MetamathPackageV1 NatZeroV1 NatZeroV1
        (TablesConsV1
          (TableV1 MetamathRowsTableV1 EntriesNilV1)
          TablesNilV1))
      MetamathConstantsGoodOccurrenceV1
      MetamathRowsTableV1
      (NatSuccV1 NatZeroV1)
      StateRequireKeyAbsentV1
      (StateRowConsV1 StateInputV1 StateRowNilV1)
      ValuesNilV1
      (RefusedKeyPresentV1
        MetamathRowsTableV1 (RowConsV1 NatZeroV1 RowNilV1))
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
      (CheckedRowPresentV1
        MetamathRowsTableV1 (RowConsV1 NatZeroV1 RowNilV1))
      $outcome $after $history)))
