!(collapse
   (let
     (gslt:result:StateStepV1:1111000
       AcceptedV1 $after $history)
     (gslt:mode:StateStepV1:1111000
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
       MetamathConstantsGoodOccurrenceV1
       (StateRequireRowV1
         MetamathRowsTableV1
         (StateRowConsV1
           (StateLiteralV1 NatZeroV1) StateRowNilV1)))
     (MetamathRequireRowAcceptedPeTTaV1 AcceptedV1)))

!(collapse
   (let
     (gslt:result:StateStepV1:1111000
       (RefusedRowAbsentV1 MetamathRowsTableV1 $row)
       $after $history)
     (gslt:mode:StateStepV1:1111000
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
       MetamathConstantsGoodOccurrenceV1
       (StateRequireRowV1
         MetamathRowsTableV1
         (StateRowConsV1
           (StateLiteralV1 (NatSuccV1 NatZeroV1)) StateRowNilV1)))
     (MetamathRequireRowMissingPeTTaV1
       (RefusedRowAbsentV1 MetamathRowsTableV1 $row))))

!(collapse
   (let
     (gslt:result:StateStepV1:1111000
       AcceptedV1 $after $history)
     (gslt:mode:StateStepV1:1111000
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
       MetamathConstantsGoodOccurrenceV1
       (StateRequireRowKeyAbsentV1
         MetamathRowsTableV1 (NatSuccV1 NatZeroV1)
         (StateRowConsV1
           (StateLiteralV1 (NatSuccV1 NatZeroV1)) StateRowNilV1)))
     (MetamathRequireRowKeyAbsentAcceptedPeTTaV1 AcceptedV1)))

!(collapse
   (let
     (gslt:result:StateStepV1:1111000
       (RefusedKeyPresentV1 MetamathRowsTableV1 $row)
       $after $history)
     (gslt:mode:StateStepV1:1111000
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
       MetamathConstantsGoodOccurrenceV1
       (StateRequireRowKeyAbsentV1
         MetamathRowsTableV1 (NatSuccV1 NatZeroV1)
         (StateRowConsV1
           (StateLiteralV1 NatZeroV1) StateRowNilV1)))
     (MetamathRequireRowKeyAbsentRefusedPeTTaV1
       (RefusedKeyPresentV1 MetamathRowsTableV1 $row))))

!(collapse
   (let
     (gslt:result:StateStepV1:1111000
       AcceptedV1 $after $history)
     (gslt:mode:StateStepV1:1111000
       MetamathPackageV1
       (StateV1
         MetamathPackageV1 NatZeroV1 NatZeroV1
         (TablesConsV1
           (TableV1 MetamathRowsTableV1 EntriesNilV1)
           TablesNilV1))
       MetamathConstantsGoodOccurrenceV1
       (StateInsertRowV1
         MetamathRowsTableV1 (NatSuccV1 NatZeroV1)
         StateRequireKeyAbsentV1
         (StateRowConsV1
           (StateLiteralV1 NatZeroV1) StateRowNilV1)))
     (MetamathInsertRowAcceptedPeTTaV1 AcceptedV1)))

!(collapse
   (let
     (gslt:result:StateStepV1:1111000
       (RefusedKeyPresentV1 MetamathRowsTableV1 $row)
       $after $history)
     (gslt:mode:StateStepV1:1111000
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
       MetamathConstantsGoodOccurrenceV1
       (StateInsertRowV1
         MetamathRowsTableV1 (NatSuccV1 NatZeroV1)
         StateRequireKeyAbsentV1
         (StateRowConsV1
           (StateLiteralV1 NatZeroV1) StateRowNilV1)))
     (MetamathInsertRowRefusedPeTTaV1
       (RefusedKeyPresentV1 MetamathRowsTableV1 $row))))
