; Diagnostic traversal of the generated production transition relation.
; Each completed step is reported before the same state is threaded onward.

(= (direct:mm:trace-actions-v1 $package $occurrence $state ActionsNilV1)
   (DirectTraceActionsResultV1 AcceptedV1 $state))

(= (direct:mm:trace-actions-v1
      $package $occurrence $before (ActionsConsV1 $action $rest))
   (let $_entered
        (println! (MetamathDirectPeTTaTraceActionEnteredV1
                    $occurrence $action))
     (let (gslt:result:StateStepV1:1111000
            $outcome $middle $history)
          (gslt:mode:StateStepV1:1111000
            $package $before $occurrence $action)
       (let $_completed
            (println! (MetamathDirectPeTTaTraceActionCompletedV1
                        $occurrence $action $outcome))
         (let (gslt:result:OutcomeClassV1:10 $class)
              (gslt:fn:OutcomeClassV1:10 $outcome)
           (case $class
             ((OutcomeRefusedV1
                (DirectTraceActionsResultV1 $outcome $before))
              (OutcomeAcceptedV1
                (direct:mm:trace-actions-v1
                  $package $occurrence $middle $rest)))))))))

(= (direct:mm:trace-stream-v1 $package $stream $occurrence $before)
   (let (gslt:result:StateOccurrenceOperationV1:10 $operation)
        (gslt:fn:StateOccurrenceOperationV1:10 $occurrence)
     (let $_entered
          (println! (MetamathDirectPeTTaTraceEnteredV1
                      $occurrence $operation))
       (let (gslt:result:StateOperationProgramV1:110 $actions)
            (gslt:fn:StateOperationProgramV1:110 $package $operation)
         (let (DirectTraceActionsResultV1 $outcome $after)
              (direct:mm:trace-actions-v1
                $package $occurrence $before $actions)
         (let $_completed
              (println! (MetamathDirectPeTTaTraceCompletedV1
                          $occurrence $operation $outcome))
           (let (gslt:result:OutcomeClassV1:10 $class)
                (gslt:fn:OutcomeClassV1:10 $outcome)
             (case $class
               ((OutcomeRefusedV1
                  MetamathDirectPeTTaTraceRefusedV1)
                (OutcomeAcceptedV1
                  (let (gslt:result:StateOccurrenceTailV1:110 $tail)
                       (gslt:fn:StateOccurrenceTailV1:110
                         $stream $occurrence)
                    (case $tail
                      ((StateOccurrenceLastTailV1
                         MetamathDirectPeTTaTraceAcceptedV1)
                       ((StateOccurrenceNextTailV1 $next)
                         (direct:mm:trace-stream-v1
                           $package $stream $next $after)))))))))))))))

!(let (gslt:result:StateOccurrenceProgramV1:00 $stream $package)
      (gslt:fn:StateOccurrenceProgramV1:00)
   (let (gslt:result:StateInitialStateV1:10 $initial)
        (gslt:fn:StateInitialStateV1:10 $package)
     (let (gslt:result:StateOccurrenceShapeV1:10 $shape)
          (gslt:fn:StateOccurrenceShapeV1:10 $stream)
       (case $shape
         ((StateOccurrenceEmptyShapeV1
            MetamathDirectPeTTaTraceAcceptedV1)
          ((StateOccurrenceNonemptyShapeV1 $first)
            (direct:mm:trace-stream-v1
              $package $stream $first $initial)))))))
