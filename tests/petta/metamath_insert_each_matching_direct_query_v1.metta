!(collapse
  (let
    (gslt:result:InsertEachMatchingV1:111111111000
      AcceptedV1
      (StateV1
        MetamathPackageV1 NatZeroV1
        (NatSuccV1 (NatSuccV1 NatZeroV1))
        (TablesConsV1
          (TableV1 NatZeroV1
            (EntriesConsV1
              (EntryV1 NatZeroV1
                (RowConsV1 NatZeroV1 RowNilV1) NatZeroV1)
              EntriesNilV1))
          (TablesConsV1
            (TableV1 (NatSuccV1 NatZeroV1)
              (EntriesConsV1
                (EntryV1 (NatSuccV1 NatZeroV1)
                  (RowConsV1 NatZeroV1
                    (RowConsV1 (NatSuccV1 NatZeroV1) RowNilV1))
                  NatZeroV1)
                EntriesNilV1))
            TablesNilV1)))
      $history)
    (gslt:mode:InsertEachMatchingV1:111111111000
      (StateV1
        MetamathPackageV1 NatZeroV1 (NatSuccV1 NatZeroV1)
        (TablesConsV1
          (TableV1 NatZeroV1
            (EntriesConsV1
              (EntryV1 NatZeroV1
                (RowConsV1 NatZeroV1 RowNilV1) NatZeroV1)
              EntriesNilV1))
          (TablesConsV1
            (TableV1 (NatSuccV1 NatZeroV1) EntriesNilV1)
            TablesNilV1)))
      MetamathConstantsGoodOccurrenceV1
      NatZeroV1
      (NatSuccV1 NatZeroV1)
      (NatSuccV1 NatZeroV1)
      StateRequireKeyAbsentV1
      (StateRowConsV1 StateInputV1 StateRowNilV1)
      (StateRowConsV1 StateInputV1
        (StateRowConsV1
          (StateLiteralV1 (NatSuccV1 NatZeroV1))
          StateRowNilV1))
      (ValuesConsV1 NatZeroV1 ValuesNilV1))
    (MetamathInsertEachMatchingMatchedPeTTaV1 AcceptedV1 $history)))

!(collapse
  (let $before
    (StateV1
      MetamathPackageV1 NatZeroV1 (NatSuccV1 NatZeroV1)
      (TablesConsV1
        (TableV1 NatZeroV1
          (EntriesConsV1
            (EntryV1 NatZeroV1
              (RowConsV1 NatZeroV1 RowNilV1) NatZeroV1)
            EntriesNilV1))
        (TablesConsV1
          (TableV1 (NatSuccV1 NatZeroV1) EntriesNilV1)
          TablesNilV1)))
    (let
      (gslt:result:InsertEachMatchingV1:111111111000
        AcceptedV1 $before $history)
      (gslt:mode:InsertEachMatchingV1:111111111000
        $before
        MetamathConstantsGoodOccurrenceV1
        NatZeroV1
        (NatSuccV1 NatZeroV1)
        (NatSuccV1 NatZeroV1)
        StateRequireKeyAbsentV1
        (StateRowConsV1 StateInputV1 StateRowNilV1)
        (StateRowConsV1 StateInputV1
          (StateRowConsV1
            (StateLiteralV1 (NatSuccV1 NatZeroV1))
            StateRowNilV1))
        (ValuesConsV1 (NatSuccV1 NatZeroV1) ValuesNilV1))
      (MetamathInsertEachMatchingMissedPeTTaV1 AcceptedV1 $history))))

!(collapse
  (let $before
    (StateV1
      MetamathPackageV1 NatZeroV1
      (NatSuccV1 (NatSuccV1 NatZeroV1))
      (TablesConsV1
        (TableV1 NatZeroV1
          (EntriesConsV1
            (EntryV1 NatZeroV1
              (RowConsV1 NatZeroV1 RowNilV1) NatZeroV1)
            EntriesNilV1))
        (TablesConsV1
          (TableV1 (NatSuccV1 NatZeroV1)
            (EntriesConsV1
              (EntryV1 (NatSuccV1 NatZeroV1)
                (RowConsV1 NatZeroV1
                  (RowConsV1 (NatSuccV1 NatZeroV1) RowNilV1))
                NatZeroV1)
              EntriesNilV1))
          TablesNilV1)))
    (let
      (gslt:result:InsertEachMatchingV1:111111111000
        (RefusedKeyPresentV1 (NatSuccV1 NatZeroV1) $existing)
        $before $history)
      (gslt:mode:InsertEachMatchingV1:111111111000
        $before
        MetamathConstantsGoodOccurrenceV1
        NatZeroV1
        (NatSuccV1 NatZeroV1)
        (NatSuccV1 NatZeroV1)
        StateRequireKeyAbsentV1
        (StateRowConsV1 StateInputV1 StateRowNilV1)
        (StateRowConsV1 StateInputV1
          (StateRowConsV1
            (StateLiteralV1 (NatSuccV1 NatZeroV1))
            StateRowNilV1))
        (ValuesConsV1 NatZeroV1 ValuesNilV1))
      (MetamathInsertEachMatchingRefusedPeTTaV1
        (RefusedKeyPresentV1 (NatSuccV1 NatZeroV1) $existing)
        $history))))
