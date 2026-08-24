(rhometta:run-values
  (gslt-rho:process:RequireEachKeyAbsentV1
    QueryMetamathRequireEachKeyAbsentCoreDepthZeroRhoCliV1
    (RequireEachKeyAbsentV1
      (StateV1
        MetamathPackageV1 NatZeroV1 NatZeroV1
        (TablesConsV1
          (TableV1 MetamathRowsTableV1 EntriesNilV1)
          TablesNilV1))
      MetamathConstantsGoodOccurrenceV1
      MetamathRowsTableV1
      (NatSuccV1 NatZeroV1)
      (StateRowConsV1 StateInputV1 StateRowNilV1)
      ValuesNilV1
      $outcome $history)))

(rhometta:run-values
  (gslt-rho:process:RequireEachKeyAbsentV1
    QueryMetamathRequireEachKeyAbsentCoreDepthOneRhoCliV1
    (RequireEachKeyAbsentV1
      (StateV1
        MetamathPackageV1 NatZeroV1 NatZeroV1
        (TablesConsV1
          (TableV1 MetamathRowsTableV1 EntriesNilV1)
          TablesNilV1))
      MetamathConstantsGoodOccurrenceV1
      MetamathRowsTableV1
      (NatSuccV1 NatZeroV1)
      (StateRowConsV1 StateInputV1 StateRowNilV1)
      (ValuesConsV1 NatZeroV1 ValuesNilV1)
      $outcome $history)))

(rhometta:run-values
  (gslt-rho:process:RequireEachKeyAbsentV1
    QueryMetamathRequireEachKeyAbsentCoreDepthOneRefusedRhoCliV1
    (RequireEachKeyAbsentV1
      (StateV1
        MetamathPackageV1 NatZeroV1 (NatSuccV1 NatZeroV1)
        (TablesConsV1
          (TableV1
            MetamathRowsTableV1
            (EntriesConsV1
              (EntryV1 NatZeroV1
                (RowConsV1 NatZeroV1 RowNilV1)
                NatZeroV1)
              EntriesNilV1))
          TablesNilV1))
      MetamathConstantsGoodOccurrenceV1
      MetamathRowsTableV1
      (NatSuccV1 NatZeroV1)
      (StateRowConsV1 StateInputV1 StateRowNilV1)
      (ValuesConsV1 NatZeroV1 ValuesNilV1)
      $outcome $history)))
