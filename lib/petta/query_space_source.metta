; Ordinary captured Space leaves for the existing compositional query library.
; Joins remain MeTTa control flow; matching, identities and candidates belong
; to the same native Space source used by match and pat.
!(import! &self "durable_space.metta")

(: kbq:open-space (-> %Undefined% Atom Number %Undefined%))
(: kbq:begin-space (-> %Undefined% Atom Atom Atom Number %Undefined%))
(= (kbq:open-space $view $plan $bytes)
  (if (and (kb:integer? $bytes) (and (> $bytes 0) (<= $bytes 1048576)))
    (case (store:view-clone $view)
      (((StoreSnapshot $copy $epoch $revision)
        (let $cursor (kbq:open (KqSpaceViewV1 $copy) $plan $bytes)
          (if (kbq:cursor? $cursor) $cursor
            (let $_ (store:close $copy) $cursor))))
       ($failure $failure)))
    (KbRefused query-row-budget)))
(= (kbq:source-validate $source)
  (if (knowledge:tagged? $source KqSpaceViewV1 2)
    (let (KqSpaceViewV1 $view) $source
      (case (store:view-info $view)
        (((StoreViewInfo $epoch $revision) KbViewReady) ($failure $failure))))
    (kb:validate-view $source)))
(= (kbq:source-observe $source)
  (if (knowledge:tagged? $source KqSpaceViewV1 2)
    (let (KqSpaceViewV1 $view) $source
      (case (store:view-info $view)
        (((StoreViewInfo $epoch $revision) (KqSpaceObservationV1 $epoch $revision))
         ($failure $failure))))
    (kb:observe-selector $source KbAll)))
(= (kbq:source-close-view $source)
  (if (knowledge:tagged? $source KqSpaceViewV1 2)
    (let (KqSpaceViewV1 $view) $source (store:close $view)) KqClosed))
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
      (if (knowledge:tagged? $source KqSpaceViewV1 2)
        (KqSpaceInspectionV1 (kbq:source-observe $source)
          (KqWork $steps $keys $bytes) (kbq:space-access $state))
        (KbRefused ordinary-space-source-required)))
    (KbRefused query-cursor)))
(= (kbq:space-access $state)
  (case $state
    (((KqSpaceStateV1 $query $sequence $maximum $keys $bytes) (store:space-query-info $query))
     ((KqRowsState $rows) KqNoActiveScanV1)
     ((KqPipeStateV1 $kind $active $config $data $maximum)
      (KqPipelineAccessV1 $kind $maximum (kbq:space-access $active)))
     ((KqUnionState $view $bytes $active $pending) (kbq:space-access $active))
     ((KqBindState $view $bytes $outer $program $mode) (kbq:space-access $outer))
     ((KqInnerState $view $bytes $outer $program $mode $inner $missing $seen $row)
      (KqActivePairV1 (kbq:space-access $outer) (kbq:space-access $inner)))
     ($other KqNoActiveScanV1))))
