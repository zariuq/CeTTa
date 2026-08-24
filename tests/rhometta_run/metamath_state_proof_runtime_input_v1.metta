!(import! &self langdef)
!(import! &self system)

(rhometta:run-values
  (let $handle (langdef:load (system:arg 1))
    (case (langdef:parse-occurrence-stream-file
            $handle (system:arg 0))
      (((LangDef:OccurrenceStreamAccepted
           $name $packDigest $stateDigest $sourceDigest
           (StateRuntimeOccurrenceStreamV1
             $package $streamIdentity $first))
         (let $closed (langdef:close $handle)
           (gslt-rho:process:StateStartOccurrenceStreamV1
             QueryMetamathStateProofRuntimeInputRhoV1
             (StateStartOccurrenceStreamV1
               $package
               (StateRuntimeOccurrenceStreamV1
                 $package $streamIdentity $first)
               $outcome $after $history))))
       ((LangDef:OccurrenceStreamRejected $name $digest $detail)
         (let $closed (langdef:close $handle)
           (rho:val
             (MetamathDirectRhoOutcomeV1 ParserRefusedV1))))
       ((LangDef:OccurrenceStreamIncomplete $name $digest $detail)
         (let $closed (langdef:close $handle)
           (rho:val
             (MetamathDirectRhoOutcomeV1 ParserIncompleteV1))))
       ($failure
         (let $closed (langdef:close $handle)
           (rho:val
             (MetamathDirectRhoOutcomeV1
               (RuntimeInputFailureV1 $failure)))))))))
