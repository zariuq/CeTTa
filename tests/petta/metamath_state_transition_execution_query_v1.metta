!(collapse
   (let $_proof
     (horn:prove-v1
       (StateStartOperationV1
         $package
         mm-declare-constants
         MetamathConstantsGoodOccurrenceV1
         AcceptedV1
         (StateV1
           $package
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
           $history)))
     (MetamathStateTransitionPeTTaGoodV1 AcceptedV1 SequenceFourV1)))

!(collapse
   (let $_proof
     (horn:prove-v1
       (StateStartOperationV1
         $package
         mm-declare-constants
         MetamathConstantsRepeatedOccurrenceV1
         (RefusedKeyPresentV1 $table $row)
         (StateV1
           $package
           NatZeroV1
           NatZeroV1
           $tables)
         (HistoryConsV1
           (EnteredActionV1
             (StateRequireDepthV1 NatZeroV1)
             MetamathConstantsRepeatedPositionV1)
           $history)))
     (MetamathStateTransitionPeTTaRepeatedV1
       RefusedKeyPresentV1 RollbackSequenceZeroV1)))
