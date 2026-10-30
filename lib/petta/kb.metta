; Durable KB facade. Storage/query primitives are shared with schema validation.
!(import! &self "kb_core.metta")

(= (kb:commit $owner $view $observation $receipt $command $answer $changes $budget)
   (case (knowledge:scopes-on $view $observation)
     (((KnowledgeReadScopesV2 $reads)
       (case (knowledge:receipt $view $receipt $command $budget)
         ((KnowledgeReceiptAbsentV1
           (case (kb:edits $changes ())
             (((KbEdits $reversed)
               (let* (($sorted (rel:order kb:edit-key-less $reversed))
                      ($edits (kb:unique-edits $sorted KbNoPreviousKey ())))
                 (case (kb:stage $view $edits $budget () () (list:reverse (cdr-atom $reads)))
                   (((KbDelta $ops $writes $all-reads)
                     (knowledge:publish-ready $owner $view (cons-atom reads $all-reads) $receipt $command $answer
                       (cons-atom ops $ops) (cons-atom writes $writes)))
                    ($error $error)))))
              ($error $error))))
          ($replayed (knowledge:replay-on $owner $view $replayed)))))
      ($error $error))))

