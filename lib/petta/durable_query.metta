; Owned data/program read observations shared by relational and graph queries.
; These are dependency/lifetime records, not completion or write authority.
!(import! &self "durable_space.metta")
(= (durable-query:context $view $watch)
  (case (store:view-clone $view)
    (((StoreSnapshot $copy $epoch $revision)
      (case (store:program-watch-clone $watch)
        (((StoreProgramWatch $guard) (DurableReadContextV1 $copy $guard))
         ($failure (let $_ (store:close $copy) $failure)))))
     ($failure $failure))))
(= (durable-query:close-context $context)
  (if (knowledge:tagged? $context DurableReadContextV1 3)
    (let (DurableReadContextV1 $view $watch) $context
      (let* (($_ (store:close $view)) ($_ (store:close $watch))) KqClosed))
    (KbRefused query-read-context)))
(: durable-query:commit (-> %Undefined% Atom Atom Atom Atom %Undefined%))
(= (durable-query:commit $owner $context $collection $request $changes)
  (let $command (SpaceCommitV1 $collection $changes)
    (durable-query:commit-command $owner $context $collection $request $command $changes)))
(: durable-query:commit-command (-> %Undefined% Atom Atom Atom Atom Atom %Undefined%))
(= (durable-query:commit-command $owner $context $collection $request $command $changes)
  (if (knowledge:tagged? $context DurableReadContextV1 3)
    (let (DurableReadContextV1 $view $watch) $context
      ; A historical receipt settles an already committed operation before
      ; old read/program observations are rechecked. This snapshot is not a
      ; replacement for the captured informing view used by a new write.
      (case (store:snapshot $owner)
        (((StoreSnapshot $fresh $epoch $revision)
          (let* (($prior (knowledge:receipt $fresh $request $command (durable-space:wire-limit)))
                 ($result (case $prior
                   ((KnowledgeReceiptAbsentV1
                     (durable-space:commit-policy $owner $view $collection $request $command $changes (ProgramWatchV1 $watch)))
                    ($found (knowledge:replay-on $owner $fresh $found)))))
                 ($_ (store:close $fresh))) $result))
         ($failure $failure))))
    (KbRefused query-read-context)))
