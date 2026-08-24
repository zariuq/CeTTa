!(collapse
  (let
    (gslt:result:CopyEachRowV1:11111111000
      AcceptedV1
      (StateV1
        MetamathPackageV1 NatZeroV1 (NatSuccV1 NatZeroV1)
        (TablesConsV1
          (TableV1 (NatSuccV1 NatZeroV1)
            (EntriesConsV1
              (EntryV1 NatZeroV1
                (RowConsV1 NatZeroV1 RowNilV1) NatZeroV1)
              EntriesNilV1))
          TablesNilV1))
      $history)
    (gslt:mode:CopyEachRowV1:11111111000
      (StateV1
        MetamathPackageV1 NatZeroV1 NatZeroV1
        (TablesConsV1
          (TableV1 (NatSuccV1 NatZeroV1) EntriesNilV1)
          TablesNilV1))
      MetamathConstantsGoodOccurrenceV1
      (NatSuccV1 NatZeroV1)
      (NatSuccV1 NatZeroV1)
      StateRequireKeyAbsentV1
      (StateRowConsV1
        (StateSourceColumnV1 NatZeroV1) StateRowNilV1)
      (EntriesConsV1
        (EntryV1 NatZeroV1
          (RowConsV1 NatZeroV1
            (RowConsV1 (NatSuccV1 (NatSuccV1 NatZeroV1)) RowNilV1))
          NatZeroV1)
        EntriesNilV1)
      NatZeroV1)
    (MetamathCopyEachRowAcceptedPeTTaV1 AcceptedV1 $history)))

!(collapse
  (let $before
    (StateV1
      MetamathPackageV1 NatZeroV1 (NatSuccV1 NatZeroV1)
      (TablesConsV1
        (TableV1 (NatSuccV1 NatZeroV1)
          (EntriesConsV1
            (EntryV1 NatZeroV1
              (RowConsV1 NatZeroV1 RowNilV1) NatZeroV1)
            EntriesNilV1))
        TablesNilV1))
    (let
      (gslt:result:CopyEachRowV1:11111111000
        (RefusedKeyPresentV1 (NatSuccV1 NatZeroV1) $existing)
        $before $history)
      (gslt:mode:CopyEachRowV1:11111111000
        $before
        MetamathConstantsGoodOccurrenceV1
        (NatSuccV1 NatZeroV1)
        (NatSuccV1 NatZeroV1)
        StateRequireKeyAbsentV1
        (StateRowConsV1
          (StateSourceColumnV1 NatZeroV1) StateRowNilV1)
        (EntriesConsV1
          (EntryV1 NatZeroV1
            (RowConsV1 NatZeroV1
              (RowConsV1 (NatSuccV1 (NatSuccV1 NatZeroV1)) RowNilV1))
            NatZeroV1)
          EntriesNilV1)
        NatZeroV1)
      (MetamathCopyEachRowRefusedPeTTaV1
        (RefusedKeyPresentV1 (NatSuccV1 NatZeroV1) $existing)
        $history))))

!(collapse
  (let
    (gslt:result:CopyEachRowMatchingV1:11111111111000
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
                  (RowConsV1
                    (NatSuccV1 (NatSuccV1 NatZeroV1)) RowNilV1)
                  NatZeroV1)
                EntriesNilV1))
            TablesNilV1)))
      $history)
    (gslt:mode:CopyEachRowMatchingV1:11111111111000
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
      (StateRowConsV1
        (StateSourceColumnV1 NatZeroV1) StateRowNilV1)
      (StateRowConsV1
        (StateSourceColumnV1 (NatSuccV1 NatZeroV1)) StateRowNilV1)
      (EntriesConsV1
        (EntryV1 NatZeroV1
          (RowConsV1 NatZeroV1
            (RowConsV1 (NatSuccV1 (NatSuccV1 NatZeroV1)) RowNilV1))
          NatZeroV1)
        EntriesNilV1)
      NatZeroV1 NatZeroV1)
    (MetamathCopyEachRowMatchingAcceptedPeTTaV1 AcceptedV1 $history)))

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
                (RowConsV1
                  (NatSuccV1 (NatSuccV1 NatZeroV1)) RowNilV1)
                NatZeroV1)
              EntriesNilV1))
          TablesNilV1)))
    (let
      (gslt:result:CopyEachRowMatchingV1:11111111111000
        (RefusedKeyPresentV1 (NatSuccV1 NatZeroV1) $existing)
        $before $history)
      (gslt:mode:CopyEachRowMatchingV1:11111111111000
        $before
        MetamathConstantsGoodOccurrenceV1
        NatZeroV1
        (NatSuccV1 NatZeroV1)
        (NatSuccV1 NatZeroV1)
        StateRequireKeyAbsentV1
        (StateRowConsV1
          (StateSourceColumnV1 NatZeroV1) StateRowNilV1)
        (StateRowConsV1
          (StateSourceColumnV1 (NatSuccV1 NatZeroV1)) StateRowNilV1)
        (EntriesConsV1
          (EntryV1 NatZeroV1
            (RowConsV1 NatZeroV1
              (RowConsV1 (NatSuccV1 (NatSuccV1 NatZeroV1)) RowNilV1))
            NatZeroV1)
          EntriesNilV1)
        NatZeroV1 NatZeroV1)
      (MetamathCopyEachRowMatchingRefusedPeTTaV1
        (RefusedKeyPresentV1 (NatSuccV1 NatZeroV1) $existing)
        $history))))

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
      (gslt:result:CopyEachRowMatchingV1:11111111111000
        AcceptedV1 $before $history)
      (gslt:mode:CopyEachRowMatchingV1:11111111111000
        $before
        MetamathConstantsGoodOccurrenceV1
        NatZeroV1
        (NatSuccV1 NatZeroV1)
        (NatSuccV1 NatZeroV1)
        StateRequireKeyAbsentV1
        (StateRowConsV1
          (StateSourceColumnV1 NatZeroV1) StateRowNilV1)
        (StateRowConsV1
          (StateSourceColumnV1 (NatSuccV1 NatZeroV1)) StateRowNilV1)
        (EntriesConsV1
          (EntryV1 NatZeroV1
            (RowConsV1 (NatSuccV1 NatZeroV1)
              (RowConsV1
                (NatSuccV1 (NatSuccV1 (NatSuccV1 NatZeroV1))) RowNilV1))
            NatZeroV1)
          EntriesNilV1)
        NatZeroV1 NatZeroV1)
      (MetamathCopyEachRowMatchingMissedPeTTaV1 AcceptedV1 $history))))
