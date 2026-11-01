; Ordinary captured Space leaves for the existing compositional query library.
; Joins remain MeTTa control flow; matching, identities and candidates belong
; to the same native Space source used by match and pat.
!(import! &self "durable_query.metta")
!(import! &self "query_range_source.metta")

(: kbq:open-space (-> %Undefined% Atom Number %Undefined%))
(: kbq:begin-space (-> %Undefined% Atom Atom Atom Number %Undefined%))
(= (kbq:open-space $view $plan $bytes)
  (if (and (kb:integer? $bytes) (and (> $bytes 0) (<= $bytes 1048576)))
    (case (store:view-clone $view)
      (((StoreSnapshot $copy $epoch $revision)
        (case (kbq:open (KqSpaceViewV1 $copy) $plan $bytes)
          (((KqCursor $source $state $steps $keys $decoded)
            (case (store:program-watch-open)
              (((StoreProgramWatch $watch)
                (KqCursor (KqSpaceViewV2 $copy $watch) $state $steps $keys $decoded))
               ($failure
                (let* (($_ (kbq:close-state $state)) ($_ (store:close $copy))) $failure)))))
           ($failure (let $_ (store:close $copy) $failure)))))
       ($failure $failure)))
    (KbRefused query-row-budget)))
(= (kbq:source-validate $source)
  (if (knowledge:tagged? $source KqSpaceViewV1 2)
    (let (KqSpaceViewV1 $view) $source
      (case (store:view-info $view)
        (((StoreViewInfo $epoch $revision) KbViewReady) ($failure $failure))))
    (kb:validate-view $source)))
(= (kbq:source-observe $source)
  (if (knowledge:tagged? $source KqSpaceViewV2 3)
    (let (KqSpaceViewV2 $view $watch) $source (kbq:source-observe (KqSpaceViewV1 $view)))
   (if (knowledge:tagged? $source KqSpaceViewV1 2)
    (let (KqSpaceViewV1 $view) $source
      (case (store:view-info $view)
        (((StoreViewInfo $epoch $revision) (KqSpaceObservationV1 $epoch $revision))
         ($failure $failure))))
    (kb:observe-selector $source KbAll))))
(= (kbq:source-close-view $source)
  (if (knowledge:tagged? $source KqSpaceViewV2 3)
    (let (KqSpaceViewV2 $view $watch) $source
      (let* (($_ (store:close $view)) ($_ (store:close $watch))) KqClosed))
   (if (knowledge:tagged? $source KqSpaceViewV1 2)
    (let (KqSpaceViewV1 $view) $source (store:close $view)) KqClosed)))
(= (kbq:program-check $source)
  (if (knowledge:tagged? $source KqSpaceViewV2 3)
    (let (KqSpaceViewV2 $view $watch) $source (store:program-watch-check $watch)) StoreProgramCurrent))

; A read context owns independent data/program observations. It may outlive
; cursor completion, but conveys no publication right or proof of completion.
(= (kbq:read-context $cursor)
  (if (kbq:cursor? $cursor)
    (let (KqCursor $source $state $steps $keys $bytes) $cursor
      (case $source
        (((KqSpaceViewV2 $view $watch) (durable-query:context $view $watch))
         ($other (KbRefused program-observation-required)))))
    (KbRefused query-cursor)))
(= (kbq:close-read-context $context) (durable-query:close-context $context))
(: kbq:commit-read (-> %Undefined% Atom Atom Atom Atom %Undefined%))
(= (kbq:commit-read $owner $context $collection $request $changes)
  (durable-query:commit $owner $context $collection $request $changes))
(: kbq:commit-read-command (-> %Undefined% Atom Atom Atom Atom Atom %Undefined%))
(= (kbq:commit-read-command $owner $context $collection $request $command $changes)
  (durable-query:commit-command $owner $context $collection $request $command $changes))
(= (kbq:begin-space $source $collection $pattern $template $bytes)
  (if (knowledge:tagged? $source KqSpaceViewV1 2)
    (let (KqSpaceViewV1 $view) $source
      (case (durable-space:cursor $view $collection $pattern $template)
        (((StoreQueryOpened $query $epoch $revision)
          (KqReady (KqSpaceStateV1 $query 0 $bytes 0 0)))
         ($failure $failure))))
    (KbRefused ordinary-space-source-required)))
(= (kbq:close-space-query $query)
  (let* (($_ (store:space-query-cancel $query)) ($_ (store:close $query))) KqClosed))
(= (kbq:advance-space $query $sequence $maximum $keys $bytes)
  (let* (($answer (store:space-query-next $query $sequence 1 $maximum))
         ($checkpoint (store:space-query-checkpoint $query)))
    (if (== $checkpoint StoreQueryCheckpointed)
      (case (store:space-query-info $query)
        (((StoreQueryInfo $scans $indexes $candidates $decoded $overflow)
          (if $overflow
            (let $_ (kbq:close-space-query $query)
              (KqFail (KbRefused query-statistics-overflow) 0 0))
            (let* (($extra (- $candidates $keys)) ($read (- $decoded $bytes)))
              (case $answer
                ((StoreQueryPaused
                  (KqSkip (KqSpaceStateV1 $query $sequence $maximum $candidates $decoded) $extra $read))
                 (StoreQueryEnd
                  (let $_ (store:close $query) (KqEnd $extra $read)))
                 ((StoreQueryRow $same $value $identities)
                  (KqOut (SpaceAnswerV1 $value $identities)
                    (KqSpaceStateV1 $query (+ $sequence 1) $maximum $candidates $decoded) $extra $read))
                 ($failure
                  (let $_ (kbq:close-space-query $query) (KqFail $failure $extra $read))))))))
         ($failure (let $_ (kbq:close-space-query $query) (KqFail $failure 0 0)))))
      (let $_ (kbq:close-space-query $query) (KqFail $checkpoint 0 0)))))

; Coordinates describe this captured root. Query-informed mutation still uses
; the original informing view and its conservative whole-revision validation.
(= (kbq:inspect-space $cursor)
  (if (kbq:cursor? $cursor)
    (let (KqCursor $source $state $steps $keys $bytes) $cursor
      (if (or (knowledge:tagged? $source KqSpaceViewV1 2) (knowledge:tagged? $source KqSpaceViewV2 3))
        (KqSpaceInspectionV1 (kbq:source-observe $source)
          (KqWork $steps $keys $bytes) (kbq:space-access $state))
        (KbRefused ordinary-space-source-required)))
    (KbRefused query-cursor)))
(= (kbq:space-access $state)
  (case $state
    (((KqSpaceStateV1 $query $sequence $maximum $keys $bytes) (store:space-query-info $query))
     ((KqRangeStateV1 $config $pending $source $point) (range-i:inspect $config $source $point))
     ((KqRowsState $rows) KqNoActiveScanV1)
     ((KqPipeStateV1 $kind $active $config $data $maximum)
      (KqPipelineAccessV1 $kind $maximum (kbq:space-access $active)))
     ((KqUnionState $view $bytes $active $pending) (kbq:space-access $active))
     ((KqBindState $view $bytes $outer $program $mode) (kbq:space-access $outer))
     ((KqInnerState $view $bytes $outer $program $mode $inner $missing $seen $row)
      (KqActivePairV1 (kbq:space-access $outer) (kbq:space-access $inner)))
     ($other KqNoActiveScanV1))))
