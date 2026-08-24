!(import! &self langdef)
!(import! &self system)

!(let $handle (langdef:load (system:arg 1))
   (let $answer
     (case (langdef:parse-occurrence-stream-file
             $handle (system:arg 0))
       (((LangDef:OccurrenceStreamAccepted
            $name $packDigest $stateDigest $sourceDigest
            (StateRuntimeOccurrenceStreamV1
              $package $streamIdentity $first))
          (let
            (gslt:result:StateStartOccurrenceStreamV1:11000
              $outcome $state $history)
            (gslt:mode:StateStartOccurrenceStreamV1:11000
              $package
              (StateRuntimeOccurrenceStreamV1
                $package $streamIdentity $first))
            (let
              (gslt:result:OutcomeClassV1:10 $class)
              (gslt:fn:OutcomeClassV1:10 $outcome)
              (case $class
                ((OutcomeAcceptedV1
                   (MetamathDirectPeTTaOutcomeV1 AcceptedV1))
                 (OutcomeRefusedV1
                   (MetamathDirectPeTTaOutcomeV1
                     (RefusedV1 $outcome))))))))
        ((LangDef:OccurrenceStreamRejected $name $digest $detail)
          (MetamathDirectPeTTaOutcomeV1 ParserRefusedV1))
        ((LangDef:OccurrenceStreamIncomplete $name $digest $detail)
          (MetamathDirectPeTTaOutcomeV1 ParserIncompleteV1))
        ($failure
          (MetamathDirectPeTTaOutcomeV1
            (RuntimeInputFailureV1 $failure)))))
     (let $closed (langdef:close $handle) $answer)))
