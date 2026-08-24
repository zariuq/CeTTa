(rhometta:run-values
  (gslt-rho:process:RequireRowV1
    QueryMetamathRequireRowCoreAcceptedRhoCliV1
    (RequireRowV1
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
      MetamathRowsTableV1
      (RowConsV1 NatZeroV1 RowNilV1)
      $outcome $history)))

(rhometta:run-values
  (gslt-rho:process:RequireRowV1
    QueryMetamathRequireRowCoreRefusedRhoCliV1
    (RequireRowV1
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
      MetamathRowsTableV1
      (RowConsV1 (NatSuccV1 NatZeroV1) RowNilV1)
      $outcome $history)))

(rhometta:run-values
  (gslt-rho:process:RequireRowKeyAbsentV1
    QueryMetamathRequireRowKeyAbsentCoreAcceptedRhoCliV1
    (RequireRowKeyAbsentV1
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
      MetamathRowsTableV1
      (NatSuccV1 NatZeroV1)
      (RowConsV1 (NatSuccV1 NatZeroV1) RowNilV1)
      $outcome $history)))

(rhometta:run-values
  (gslt-rho:process:RequireRowKeyAbsentV1
    QueryMetamathRequireRowKeyAbsentCoreRefusedRhoCliV1
    (RequireRowKeyAbsentV1
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
      MetamathRowsTableV1
      (NatSuccV1 NatZeroV1)
      (RowConsV1 NatZeroV1 RowNilV1)
      $outcome $history)))
