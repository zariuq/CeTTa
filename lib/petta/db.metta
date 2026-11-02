; Database connections use the native publication owner. Snapshots and the
; transaction callback's argument are ordinary Spaces; there is no DB matcher.
!(import! &self "kb_query.metta")
!(import! &self (library system))
!(import! &self (library act))
!(import! &self (library fs))

(= (db:new-request)
  (case (system:uuid4-hex)
    (((Error $where $why) (Error $where $why))
     ($value (str:concat "" $value)))))

(: db:open (-> %Undefined% %Undefined%))
(= (db:open $path)
  (let $text (str:concat "" $path)
  (if (fs:is-file $text)
    (let $opened (act:open $text)
      (case $opened (((Error $where $why) (Error $where $why)) ($space (db:readonly $space)))))
    (db:open-directory $text))))
(= (db:open-directory $text)
  (case (store:open $text "create-act-directory")
    (((StoreOpened $owner)
      (case (db:ensure $owner data ())
        ((True $owner)
         ($failure (let $_ (store:close $owner) (db:error db:open $failure))))))
     ($failure (db:error db:open $failure)))))

(: db:open (-> %Undefined% %Undefined% %Undefined%))
(= (db:open $name $path)
  (case (db:open $path)
    (((Error $where $why) (Error $where $why))
     ($owner (let $_ (change-state! $name $owner) $owner)))))
; PeTTa's named state holds a connection across top-level directives.
; bind! retains its reference behavior for new-space/new-state declarations.
(= (db:resource $value)
  (if (== (get-metatype $value) Symbol) (catch (get-state $value)) $value))

; Ensure a named collection. Index declarations are explicit and immutable
; here; use the existing reindex library to change an established layout.
(: db:ensure (-> %Undefined% Atom Atom %Undefined%))
(= (db:ensure $resource $collection $indexes)
  (let $owner (db:resource $resource) (db:ensure-owner $owner $collection $indexes)))
(= (db:ensure-store $owner $collection $indexes)
  (case (store:snapshot $owner)
    (((StoreSnapshot $view $epoch $revision)
      (let* (($ready (db:ensure-view $owner $view $collection $indexes))
             ($_ (store:close $view))) $ready))
     ($failure (db:error db:ensure $failure)))))
(= (db:ensure-view $owner $view $collection $indexes)
  (case (durable-space:metadata $collection)
    (((KnowledgeAddressV1 $key)
      (case (store:get $view $key 0)
        ((StoreAbsent
          (case (db:new-request)
            (((Error $where $why) (Error $where $why))
             ($request
              (case (durable-space:create-indexed $owner $view $collection $request $indexes)
                (((KnowledgeCommittedV1 $_ $_ $_) True)
                 ($failure (db:error db:ensure $failure))))))))
         ((StoreValue $_) (db:ensure-existing $view $collection $key $indexes))
         ((StoreTooLarge $_) (db:ensure-existing $view $collection $key $indexes))
         ($failure (db:error db:ensure $failure)))))
     ($failure (db:error db:ensure $failure)))))
(= (db:ensure-existing $view $collection $key $indexes)
  (case (durable-space:prefix $collection)
    (((KnowledgeAddressV1 $prefix)
      (case (durable-space:state $view $collection $key $prefix)
        (((SpaceStateV1 $_ $declared)
          (if (== $indexes ()) True
            (case (durable-index:descriptors $collection $indexes)
              (((SpaceIndexesV1 $wanted)
                (if (== $wanted $declared) True (Error db:ensure index-layout-mismatch)))
               ($failure (db:error db:ensure $failure))))))
         ($failure (db:error db:ensure $failure)))))
     ($failure (db:error db:ensure $failure)))))

(: db:snapshot (-> %Undefined% %Undefined%))
(: db:snapshot (-> %Undefined% Atom %Undefined%))
(= (db:snapshot $owner) (db:snapshot $owner data))
(= (db:snapshot $resource $collection)
  (let $owner (db:resource $resource) (db:snapshot-owner $owner $collection)))
(= (db:snapshot-store $owner $collection)
  (case (store:snapshot $owner)
    (((StoreSnapshot $view $epoch $revision)
      (let* (($result (durable-space:capture $view $collection))
             ($_ (store:close $view)))
        (case $result
          (((StoreSpace $space $_ $_) $space)
           ($failure (db:error db:snapshot $failure))))))
     ($failure (db:error db:snapshot $failure)))))

; No automatic retry: a callback can have effects outside this transaction.
; It runs once with one captured workspace. Empty/error/incomplete outcomes
; publish nothing. Portable successful answers are stored with the receipt.
(: db:transaction (-> %Undefined% Atom %Undefined%))
(: db:transaction (-> %Undefined% Atom Atom %Undefined%))
(= (db:transaction $owner $body) (db:transaction $owner data $body))
(= (db:transaction $owner $collection $body)
  (case (db:new-request)
    (((Error $where $why) (Error $where $why))
     ($request
      (case (db:transaction-result $owner $collection $request automatic $body)
        (((db:committed $_ $values) (superpose $values))
         ((db:replayed $_ $values) (superpose $values))
         ((db:aborted $_) (empty))
         ($failure $failure)))))))

; Services retain REQUEST and OPERATION before submitting. A matching durable
; receipt returns its original answers before the callback is considered.
(: db:transaction-result (-> %Undefined% Atom Atom Atom Atom %Undefined%))
(= (db:transaction-result $resource $collection $request $operation $body)
  (let $owner (db:resource $resource)
    (db:transaction-owner $owner $collection $request $operation $body)))
(= (db:transaction-store $owner $collection $request $operation $body)
  (let $command (DbTransactionV1 $collection $operation)
    (case (store:snapshot $owner)
      (((StoreSnapshot $view $epoch $revision)
        (let* (($result (db:transaction-view $owner $view $collection $request $command $body))
               ($_ (store:close $view))) $result))
       ($failure (db:error db:transaction $failure))))))
(= (db:transaction-view $owner $view $collection $request $command $body)
  (case (knowledge:receipt $view $request $command (durable-space:wire-limit))
    ((KnowledgeReceiptAbsentV1
      (case (store:program-watch-open)
        (((StoreProgramWatch $watch)
          (let* (($result (db:edit-view $owner $view $collection $request $command $body $watch))
                 ($_ (store:close $watch))) $result))
         ($failure (db:error db:transaction $failure)))))
     ($prior (db:outcome $request (knowledge:replay-on $owner $view $prior))))))
(= (db:edit-view $owner $view $collection $request $command $body $watch)
  (case (durable-space:metadata $collection)
    (((KnowledgeAddressV1 $key)
      (case (store:space-edit $view $key $body $watch)
        (((StoreSpaceEdited $values $added $removed)
          (let* (($deletes (map-atom $removed $reference (SpaceDeleteV1 $reference)))
                 ($inserts (durable-space:workspace-inserts $added 0))
                 ($changes (list:append $deletes $inserts)))
            (db:outcome $request (durable-space:commit-policy-result $owner $view $collection $request $command
              $changes (ProgramWatchV1 $watch) (DbTransactionValuesV1 $values)))))
         (StoreSpaceAborted (db:aborted $request))
         ((StoreSpaceFailed $error) $error)
         ($failure (db:error db:transaction $failure)))))
     ($failure (db:error db:transaction $failure)))))
(= (db:outcome $request $result)
  (case $result
    (((KnowledgeCommittedV1 (DbTransactionValuesV1 $values) $_ $_) (db:committed $request $values))
     ((KnowledgeReplayedV1 (DbTransactionValuesV1 $values)) (db:replayed $request $values))
     ($failure (db:error db:transaction $failure)))))

(: db:close (-> %Undefined% %Undefined%))
(= (db:close $resource)
  (case $resource
    (((db:cursor $native)
      (case (kbq:close $native) ((KqClosed True) ($failure (db:error db:close $failure)))))
     ($ordinary (let $owner (db:resource $ordinary) (db:close-owner $owner))))))
; Keep errors inspectable and preserve an original body/library error atom.
(= (db:error $call $failure)
  (case $failure
    (((Error $where $reason) $failure)
     ($other (Error $call $other)))))

; Paged matching uses the existing compositional cursor and ordinary matcher.
; Work is an explicit quantum, not a result cap. Paused carries its cursor.
(: db:query (-> %Undefined% Atom Atom %Undefined%))
(: db:query (-> %Undefined% Atom Atom Atom %Undefined%))
(= (db:query $owner $pattern $template) (db:query $owner data $pattern $template))
(= (db:query $owner $collection $pattern $template)
  (db:query-plan $owner (KqMap (KqSpace $collection $pattern $template) db:public-row)))
(: db:query-plan (-> %Undefined% Atom %Undefined%))
(= (db:query-plan $resource $plan) (db:query-plan-owner (db:resource $resource) $plan))
(= (db:query-plan-store $owner $plan)
    (case (store:snapshot $owner)
      (((StoreSnapshot $view $_ $_)
        (let* (($opened (kbq:open-space $view $plan (durable-space:wire-limit)))
               ($_ (store:close $view)))
          (if (kbq:cursor? $opened) (db:cursor $opened) (db:error db:query $opened))))
       ($failure (db:error db:query $failure)))))
(: db:page (-> Atom Number Number %Undefined%))
(= (db:page $cursor $count $work)
  (case $cursor
    (((db:cursor $native)
      (case (kbq:page $native $count $work)
        (((KqPage $rows (KqMore $next)) (db:page-result $rows (db:more (db:cursor $next))))
         ((KqPage $rows (KqPaused $next)) (db:page-result $rows (db:paused (db:cursor $next))))
         ((KqPage $rows (KqComplete $_)) (db:page-result $rows db:complete))
         ((KqPage $rows (KqFailure $why $_)) (db:page-result $rows (db:failed $why)))
         ($failure (db:error db:page $failure)))))
     ($failure (Error db:page invalid-cursor)))))

(: db:resolve (-> %Undefined% Atom Atom Atom %Undefined%))
(= (db:resolve $resource $collection $request $operation)
  (let $owner (db:resource $resource)
    (case (knowledge:resolve $owner $request (DbTransactionV1 $collection $operation) (durable-space:wire-limit))
      (((KnowledgeResolvedV1 (DbTransactionValuesV1 $values) $_ $_) (db:resolved $request $values))
       ((KnowledgeUnrecordedV1 $_ $_) (db:unrecorded $request))
       ($failure (db:error db:resolve $failure))))))

; One-operation convenience calls use the same scoped transaction path.
(: db:add (-> %Undefined% Atom %Undefined%))
(= (db:add $owner $atom)
  (db:transaction $owner (|-> ($space) (add-atom $space $atom))))
(: db:remove (-> %Undefined% Atom %Undefined%))
(: db:remove (-> %Undefined% Atom Number %Undefined%))
(= (db:remove $owner $pattern)
  (db:transaction $owner (|-> ($space) (remove-atom $space $pattern))))
(= (db:remove $owner $pattern $maximum)
  (db:transaction $owner (|-> ($space) (remove-atom $space $pattern $maximum))))

; Portable immutable snapshots retain saved occurrence identities. Importing
; copies atoms into this database's identity domain in one transaction.
(: db:export-act (-> %Undefined% String %Undefined%))
(: db:export-act (-> %Undefined% Atom String %Undefined%))
(= (db:export-act $owner $path) (db:export-act $owner data $path))
(= (db:export-act $owner $collection $path)
  (case (db:snapshot $owner $collection)
    (((Error $where $why) (Error $where $why))
     ($space
      (let* (($result (catch (act:dump! $space $path)))
             ($_ (db:close $space))) $result)))))
(: db:import-act (-> %Undefined% String %Undefined%))
(: db:import-act (-> %Undefined% Atom String %Undefined%))
(= (db:import-act $owner $path) (db:import-act $owner data $path))
(= (db:import-act $owner $collection $path)
  (db:transaction $owner $collection (|-> ($space) (act:load! $space $path))))

; The simple query exposes template values and occurrence identities in a
; public row. Quote preserves callable-looking payloads as returned data.
(: db:public-row (-> Atom %Undefined%))
(= (db:public-row (SpaceAnswerV1 $value $identities))
  (quote (db:row $value $identities)))

; Maintenance changes representation while retaining the logical revision.
(: db:compact (-> %Undefined% %Undefined%))
(= (db:compact $resource) (db:compact-owner (db:resource $resource)))
(= (db:compact-store $owner)
    (case (store:compact-act $owner)
      (((StoreSnapshot $view $_ $_)
        (let* (($_ (store:close $view)) ($_ (store:collect $owner))) True))
       ($failure (db:error db:compact $failure)))))
(: db:info (-> %Undefined% %Undefined%))
(= (db:info $resource) (db:info-owner (db:resource $resource)))
(= (db:info-store $owner)
  (case (store:snapshot $owner)
      (((StoreSnapshot $view $_ $_)
        (let* (($info (store:storage-info $view)) ($_ (store:close $view)))
          (case $info
            (((StoreStorageInfo $format $epoch $revision $base $delta $frozen $has-base $has-frozen)
              (db:database-info (format $format) (epoch $epoch) (revision $revision)
                (base-bytes $base) (delta-bytes $delta) (frozen-bytes $frozen)
                (has-base $has-base) (compacting $has-frozen)))
             ($failure (db:error db:info $failure))))))
       ($failure (db:error db:info $failure)))))

; A file connection owns one immutable source. Each returned snapshot owns an
; independent capture; closing the connection leaves its children readable.
(= (db:snapshot-owner $owner $collection)
  (case $owner
    (((db:readonly $space) (if (== $collection data) (act:snapshot $space) (Error db:snapshot collection-absent)))
     ($other (db:snapshot-store $owner $collection)))))
(= (db:ensure-owner $owner $collection $indexes)
  (case $owner
    (((db:readonly $space) (if (and (== $collection data) (== $indexes ())) True (Error db:ensure read-only)))
     ($other (db:ensure-store $owner $collection $indexes)))))
(= (db:transaction-owner $owner $collection $request $operation $body)
  (case $owner
    (((db:readonly $space) (Error db:transaction read-only))
     ($other (db:transaction-store $owner $collection $request $operation $body)))))
(= (db:close-owner $owner)
  (case $owner
    (((db:readonly $space) (act:close! $space))
     ($other
      (case (store:close $owner)
        ((StoreClosed True)
         ($failure (case (act:close! $owner) ((True True) ($other (db:error db:close $failure)))))))))))
(= (db:query-plan-owner $owner $plan)
  (case $owner
    (((db:readonly $space)
      (let $opened (kbq:open-captured $space $plan (durable-space:wire-limit))
        (if (kbq:cursor? $opened) (db:cursor $opened) (db:error db:query $opened))))
     ($other (db:query-plan-store $owner $plan)))))
(= (db:compact-owner $owner)
  (case $owner
    (((db:readonly $space) (Error db:compact read-only))
     ($other (db:compact-store $owner)))))
(= (db:info-owner $owner)
  (case $owner
    (((db:readonly $space)
      (let $captured (act:snapshot $space)
        (case $captured
          (((Error $where $why) (Error $where $why))
           ($copy (let $_ (act:close! $copy) (db:database-info (format ACT) (read-only True))))))))
     ($other (db:info-store $owner)))))
