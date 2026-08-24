; Diagnostic only: execute the four authored mm-declare-constants actions
; through the direct StateStep mode while keeping each intermediate state in
; the next call's ground input position.  The production compiler must derive
; this schedule itself; this query is not an alternative execution path.

!(collapse
  (let
    (gslt:result:StateInitialStateV1:10 $state0)
    (gslt:fn:StateInitialStateV1:10
      (StateProgramDigestV1 sha256-43359b73f5920b7172d974e5aa8c1caad3678e57e7248880dd3f95c74f835c76))
  (let
    (gslt:result:StateOperationProgramV1:110
      (ActionsConsV1 $action1
        (ActionsConsV1 $action2
          (ActionsConsV1 $action3
            (ActionsConsV1 $action4 ActionsNilV1)))))
    (gslt:fn:StateOperationProgramV1:110
      (StateProgramDigestV1 sha256-43359b73f5920b7172d974e5aa8c1caad3678e57e7248880dd3f95c74f835c76)
      mm-declare-constants)
  (let
    (gslt:result:StateStepV1:1111000 AcceptedV1 $state1 $history1)
    (gslt:mode:StateStepV1:1111000
      (StateProgramDigestV1 sha256-43359b73f5920b7172d974e5aa8c1caad3678e57e7248880dd3f95c74f835c76)
      $state0 MetamathConstantsGoodOccurrenceV1 $action1)
  (let
    (gslt:result:StateStepV1:1111000 AcceptedV1 $state2 $history2)
    (gslt:mode:StateStepV1:1111000
      (StateProgramDigestV1 sha256-43359b73f5920b7172d974e5aa8c1caad3678e57e7248880dd3f95c74f835c76)
      $state1 MetamathConstantsGoodOccurrenceV1 $action2)
  (let
    (gslt:result:StateStepV1:1111000 AcceptedV1 $state3 $history3)
    (gslt:mode:StateStepV1:1111000
      (StateProgramDigestV1 sha256-43359b73f5920b7172d974e5aa8c1caad3678e57e7248880dd3f95c74f835c76)
      $state2 MetamathConstantsGoodOccurrenceV1 $action3)
  (let
    (gslt:result:StateStepV1:1111000 AcceptedV1 $state4 $history4)
    (gslt:mode:StateStepV1:1111000
      (StateProgramDigestV1 sha256-43359b73f5920b7172d974e5aa8c1caad3678e57e7248880dd3f95c74f835c76)
      $state3 MetamathConstantsGoodOccurrenceV1 $action4)
  (let
    (StateV1
      (StateProgramDigestV1 sha256-43359b73f5920b7172d974e5aa8c1caad3678e57e7248880dd3f95c74f835c76)
      NatZeroV1
      (NatSuccV1 (NatSuccV1 (NatSuccV1 (NatSuccV1 NatZeroV1))))
      $tables4)
    $state4
    (MetamathManualGroundChainGoodV1 AcceptedV1 SequenceFourV1)))))))))

!(collapse
  (let
    (gslt:result:StateInitialStateV1:10 $state0)
    (gslt:fn:StateInitialStateV1:10
      (StateProgramDigestV1 sha256-43359b73f5920b7172d974e5aa8c1caad3678e57e7248880dd3f95c74f835c76))
  (let
    (gslt:result:StateOperationProgramV1:110
      (ActionsConsV1 $action1
        (ActionsConsV1 $action2
          (ActionsConsV1 $action3
            (ActionsConsV1 $action4 ActionsNilV1)))))
    (gslt:fn:StateOperationProgramV1:110
      (StateProgramDigestV1 sha256-43359b73f5920b7172d974e5aa8c1caad3678e57e7248880dd3f95c74f835c76)
      mm-declare-constants)
  (let
    (gslt:result:StateStepV1:1111000 AcceptedV1 $state1 $history1)
    (gslt:mode:StateStepV1:1111000
      (StateProgramDigestV1 sha256-43359b73f5920b7172d974e5aa8c1caad3678e57e7248880dd3f95c74f835c76)
      $state0 MetamathConstantsRepeatedOccurrenceV1 $action1)
  (let
    (gslt:result:StateStepV1:1111000 AcceptedV1 $state2 $history2)
    (gslt:mode:StateStepV1:1111000
      (StateProgramDigestV1 sha256-43359b73f5920b7172d974e5aa8c1caad3678e57e7248880dd3f95c74f835c76)
      $state1 MetamathConstantsRepeatedOccurrenceV1 $action2)
  (let
    (gslt:result:StateStepV1:1111000
      (RefusedKeyPresentV1 $table $row) $state3 $history3)
    (gslt:mode:StateStepV1:1111000
      (StateProgramDigestV1 sha256-43359b73f5920b7172d974e5aa8c1caad3678e57e7248880dd3f95c74f835c76)
      $state2 MetamathConstantsRepeatedOccurrenceV1 $action3)
  (let
    (StateV1
      (StateProgramDigestV1 sha256-43359b73f5920b7172d974e5aa8c1caad3678e57e7248880dd3f95c74f835c76)
      NatZeroV1 NatZeroV1 $tables3)
    $state3
    (MetamathManualGroundChainRepeatedV1
      RefusedKeyPresentV1 RollbackSequenceZeroV1))))))))
