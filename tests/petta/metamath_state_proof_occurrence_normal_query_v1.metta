!(unique
   (let* (($proof
            (gslt:StateStartOccurrenceStreamV1 (StateStartOccurrenceStreamV1
              $package $stream AcceptedV1 $state $history)))
           ($checked
            (gslt:HistoryContainsCheckedProofV1 (HistoryContainsCheckedProofV1
              $history
              (ProofTraceConsV1
                (ProofTracePushV1
                  (ProofSequenceConsV1
                    (ProofRelationalTokenV1
                      (StateProofOwnerV1
                        $machine $owner-package $owner-state $owner-stream
                        $proof-occurrence $assertion)
                      $typecode)
                    $body))
                $rest))))
           ($position
            (gslt:OccurrencePositionV1 (OccurrencePositionV1
              $proof-occurrence
              (SourcePositionV1
                $source-digest $start $end $line-start $line-end)))))
     (MetamathStateProofOccurrenceNormalV1
       AcceptedV1 CheckedProofV1 $start $end)))
