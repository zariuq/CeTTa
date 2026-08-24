!(gslt-rho:query:StateStepV1
   QueryMetamathRequireRowAcceptedV1
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
     MetamathConstantsGoodOccurrenceV1
     (StateRequireRowV1
       MetamathRowsTableV1
       (StateRowConsV1
         (StateLiteralV1 NatZeroV1) StateRowNilV1))
     $outcome $after $history))

!(gslt-rho:query:StateStepV1
   QueryMetamathRequireRowMissingV1
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
     MetamathConstantsGoodOccurrenceV1
     (StateRequireRowV1
       MetamathRowsTableV1
       (StateRowConsV1
         (StateLiteralV1 (NatSuccV1 NatZeroV1)) StateRowNilV1))
     $outcome $after $history))

!(gslt-rho:query:StateStepV1
   QueryMetamathRequireRowKeyAbsentAcceptedV1
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
     MetamathConstantsGoodOccurrenceV1
     (StateRequireRowKeyAbsentV1
       MetamathRowsTableV1 (NatSuccV1 NatZeroV1)
       (StateRowConsV1
         (StateLiteralV1 (NatSuccV1 NatZeroV1)) StateRowNilV1))
     $outcome $after $history))

!(gslt-rho:query:StateStepV1
   QueryMetamathRequireRowKeyAbsentRefusedV1
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
     MetamathConstantsGoodOccurrenceV1
     (StateRequireRowKeyAbsentV1
       MetamathRowsTableV1 (NatSuccV1 NatZeroV1)
       (StateRowConsV1
         (StateLiteralV1 NatZeroV1) StateRowNilV1))
     $outcome $after $history))

!(gslt-rho:query:StateStepV1
   QueryMetamathInsertRowAcceptedV1
   (StateStepV1
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
         (StateLiteralV1 NatZeroV1) StateRowNilV1))
     $outcome $after $history))

!(gslt-rho:query:StateStepV1
   QueryMetamathInsertRowRefusedV1
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
     MetamathConstantsGoodOccurrenceV1
     (StateInsertRowV1
       MetamathRowsTableV1 (NatSuccV1 NatZeroV1)
       StateRequireKeyAbsentV1
       (StateRowConsV1
         (StateLiteralV1 NatZeroV1) StateRowNilV1))
     $outcome $after $history))
