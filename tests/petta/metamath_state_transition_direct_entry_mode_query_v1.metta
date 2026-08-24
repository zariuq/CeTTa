!(collapse
   (let
     (gslt:result:StateStartOperationV1:111000
       AcceptedV1
       (StateV1
         (StateProgramDigestV1 sha256-43359b73f5920b7172d974e5aa8c1caad3678e57e7248880dd3f95c74f835c76)
         NatZeroV1
         (NatSuccV1
           (NatSuccV1
             (NatSuccV1
               (NatSuccV1 NatZeroV1))))
         $tables)
       (HistoryConsV1
         (EnteredActionV1
           (StateRequireDepthV1 NatZeroV1)
           MetamathConstantsGoodPositionV1)
         $history))
     (gslt:mode:StateStartOperationV1:111000
       (StateProgramDigestV1 sha256-43359b73f5920b7172d974e5aa8c1caad3678e57e7248880dd3f95c74f835c76)
       mm-declare-constants
       MetamathConstantsGoodOccurrenceV1)
     (MetamathStateTransitionDirectEntryModeGoodV1
       AcceptedV1 SequenceFourV1)))

!(collapse
   (let
     (gslt:result:StateStartOperationV1:111000
       (RefusedKeyPresentV1 $table $row)
       (StateV1
         (StateProgramDigestV1 sha256-43359b73f5920b7172d974e5aa8c1caad3678e57e7248880dd3f95c74f835c76)
         NatZeroV1 NatZeroV1 $tables)
       (HistoryConsV1
         (EnteredActionV1
           (StateRequireDepthV1 NatZeroV1)
           MetamathConstantsRepeatedPositionV1)
         $history))
     (gslt:mode:StateStartOperationV1:111000
       (StateProgramDigestV1 sha256-43359b73f5920b7172d974e5aa8c1caad3678e57e7248880dd3f95c74f835c76)
       mm-declare-constants
       MetamathConstantsRepeatedOccurrenceV1)
     (MetamathStateTransitionDirectEntryModeRepeatedV1
       RefusedKeyPresentV1 RollbackSequenceZeroV1)))
