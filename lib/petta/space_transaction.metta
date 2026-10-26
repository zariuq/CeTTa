; Native PeTTa transactions over the named-space service. Every read, including
; absence, is tied to one revision; commits publish the whole batch or refuse.
; Reads: (scope LABEL PATTERN TEMPLATE). Writes: a list of matching patterns.
; Snapshot allowances are explicit resource bounds, never silent truncation.
; All snapshot owners are closed after consumption or a reported failure.
; Use native-ordered when insertion order must survive removals.
;
; begin/publish additionally keep (SpaceReceiptV1 KEY FINGERPRINT RESULT) in
; the application store, atomically with the transition. They never retry an
; uncertain write and never execute an external effect. Receipt records are
; reserved and survive the service's bounded transport-reply cache.
!(import! &self (library space_service))
!(import! &self (library io))

(: space:observe (-> String Atom Atom Atom Number Number %Undefined%))

(= (space:observe $socket $handle $reads $writes $limit $lease)
  (case (space:describe $socket $handle)
    (((reply $rid ok $current)
       (case (space-tx:read-scopes $socket $current $reads $limit $lease ())
         (((SpaceReadResults $results) (SpaceObserved $current $reads $writes $results))
           ($failed $failed)))) ($failed $failed))))

(= (space-tx:revision (space $uuid $id $name $backend $kind $rev $count)) $rev)

(= (space-tx:read-scopes $socket $handle $remaining $limit $lease $results)
  (if (== $remaining ()) (SpaceReadResults $results)
    (case (car-atom $remaining)
      (((scope $label $pattern $template)
         (case (space:query $socket $handle $pattern $template $limit $lease)
           (((reply $rid ok $snapshot)
              (case (space-tx:read-snapshot $socket $handle $snapshot)
                (((SpaceRows $rows)
                   (let $next (union-atom $results ((SpaceRead $label $rows)))
                     (space-tx:read-scopes $socket $handle (cdr-atom $remaining) $limit $lease $next)))
                  ($failed $failed)))) ($failed $failed))))
        ($invalid (SpaceRefused invalid-read-scope))))))

(= (space-tx:read-snapshot $socket $handle (snapshot $boot $cursor $source $rev $total $lease))
  (let $snapshot (snapshot $boot $cursor $source $rev $total $lease)
    (let $answer
      (if (== $rev (space-tx:revision $handle)) (space-tx:pages $socket $snapshot 0 128 ())
        (SpaceRefused stale-observation)) (let $closed (space:close $socket $snapshot) $answer))))

(= (space-tx:pages $socket $snapshot $offset $count $accumulated)
  (case (space:fetch $socket $snapshot $offset $count)
    (((reply $rid ok (page $rev $start $rows $next $done))
       (let $page-rows (cdr-atom $rows)
         (let $all (union-atom $accumulated $page-rows)
           (if $done (SpaceRows $all) (space-tx:pages $socket $snapshot $next $count $all)))))
      ((reply $rid error page-byte-limit)
        (if (> $count 1) (space-tx:pages $socket $snapshot $offset (/ (- $count (% $count 2)) 2) $accumulated)
          (reply $rid error page-byte-limit))) ($failed $failed))))

(: space-tx:covered (-> Atom Atom %Undefined%))

(= (space-tx:covered $patterns $value)
  (if (== $patterns ()) False
    (let* (($pattern (car-atom $patterns)) ($rest (cdr-atom $patterns)))
      (let $matches (collapse (case $value (($pattern True) ($other False))))
        (if (car-atom $matches) True (space-tx:covered $rest $value))))))

(: space-tx:operations-covered (-> Atom Atom %Undefined%))

(= (space-tx:operations-covered $patterns $operations)
  (if (== $operations ()) True
    (let* (($operation (car-atom $operations)) ($rest (cdr-atom $operations)))
      (case $operation
        (((add $value)
           (if (space-tx:covered $patterns $value) (space-tx:operations-covered $patterns $rest) False))
          ((remove $value)
            (if (space-tx:covered $patterns $value) (space-tx:operations-covered $patterns $rest) False))
          ($invalid False))))))

(: space:commit (-> String Atom String Atom %Undefined%))

(= (space:commit $socket $observation $request-id $operations)
  (case $observation
    (((SpaceObserved (space $uuid $id $name $backend $kind $rev $count) $reads $writes $results)
       (if (space-tx:operations-covered $writes $operations)
         (let $batch (cons-atom ops $operations)
           (space:call $socket (request $request-id batch $uuid $id $rev $batch) 5000))
         (SpaceRefused undeclared-write)))
      ((SpaceScopedObserved $handle $ticket $reads $writes $results)
        (space-scope:commit $socket $handle $ticket $request-id $operations))
      ($invalid (SpaceRefused invalid-observation)))))

; Scoped observations capture full records atomically in the service. The
; caller's read declarations are (scope LABEL PATTERN), with no projection:
; rel:project and other application queries consume the returned records.
; The service validates the declared views and writes in one publication.
(: space:observe-scoped (-> String Atom Atom Atom Number Number %Undefined%))
(= (space:observe-scoped $socket $handle $reads $writes $limit $lease)
  (case (space-scope:patterns $reads)
    (((SpaceScopePatterns $patterns)
       (case (space-scope:open $socket $handle $patterns $writes $limit $lease)
         (((reply $rid ok $ticket)
            (case (space-scope:read-results $socket $ticket $reads 0 ())
              (((SpaceReadResults $results)
                 (SpaceScopedObserved $handle $ticket $reads $writes $results))
                ($failed
                  (let $closed (space-scope:close $socket $ticket) $failed)))))
           ($failed $failed))))
      ($invalid $invalid))))

(: space-scope:patterns (-> Atom %Undefined%))
(= (space-scope:patterns $reads)
  (if (== $reads ()) (SpaceScopePatterns ())
    (case (car-atom $reads)
      (((scope $label $pattern)
         (let $rest (cdr-atom $reads)
           (case (space-scope:patterns $rest)
             (((SpaceScopePatterns $tail)
                (let $next (cons-atom $pattern $tail) (SpaceScopePatterns $next)))
               ($failed $failed)))))
        ($invalid (SpaceRefused invalid-read-scope))))))

(= (space-scope:has-capability $capabilities)
  (if (== $capabilities ()) False
    (if (== (car-atom $capabilities) scoped-observations-v1) True
      (space-scope:has-capability (cdr-atom $capabilities)))))

(: space-scope:open (-> String Atom Atom Atom Number Number %Undefined%))
(= (space-scope:open $socket (space $uuid $id $name $backend $kind $rev $count)
     $reads $writes $limit $lease)
  (let $hello (space:request-id)
    (case (space:call $socket (request $hello hello) 5000)
      (((reply $rid ok (service $version $current-uuid $boot $catalog $capabilities))
         (if (space-scope:has-capability (cdr-atom $capabilities))
           (let* (($read-values (cons-atom reads $reads))
                  ($write-values (cons-atom writes $writes)) ($request-id (space:request-id)))
             (space:call $socket
               (request $request-id observe $uuid $id $read-values $write-values $limit $lease) 5000))
           (SpaceRefused unsupported-scoped-observations)))
        ($failed $failed)))))

(: space-scope:fetch (-> String Atom Number Number Number %Undefined%))
(= (space-scope:fetch $socket (observation $boot $id $source $rev $reads $writes $lease)
     $scope $offset $count)
  (let $rid (space:request-id)
    (space:call $socket (request $rid observe-fetch $boot $id $scope $offset $count) 5000)))

(: space-scope:close (-> String Atom %Undefined%))
(= (space-scope:close $socket (observation $boot $id $source $rev $reads $writes $lease))
  (let $rid (space:request-id)
    (space:call $socket (request $rid observe-close $boot $id) 5000)))

(: space-scope:pages (-> String Atom Number Number Number Atom %Undefined%))
(= (space-scope:pages $socket $ticket $scope $offset $count $collected)
  (case (space-scope:fetch $socket $ticket $scope $offset $count)
    (((reply $rid ok (page $rev $start $rows $next $done))
       (let* (($page (cdr-atom $rows)) ($all (union-atom $collected $page)))
         (if $done (SpaceRows $all)
           (space-scope:pages $socket $ticket $scope $next $count $all))))
      ((reply $rid error page-byte-limit)
        (if (> $count 1) (space-scope:pages $socket $ticket $scope $offset (/ (- $count (% $count 2)) 2) $collected)
          (reply $rid error page-byte-limit)))
      ($failed $failed))))

(: space-scope:read-results (-> String Atom Atom Number Atom %Undefined%))
(= (space-scope:read-results $socket $ticket $reads $index $collected)
  (if (== $reads ()) (SpaceReadResults $collected)
    (let (scope $label $pattern) (car-atom $reads)
      (case (space-scope:pages $socket $ticket $index 0 128 ())
        (((SpaceRows $rows)
           (let* (($all (union-atom $collected ((SpaceRead $label $rows)))) ($rest (cdr-atom $reads)))
             (space-scope:read-results $socket $ticket $rest (+ $index 1) $all)))
          ($failed $failed))))))

(: space-scope:commit (-> String Atom Atom String Atom %Undefined%))
(= (space-scope:commit $socket
     (space $uuid $source $name $backend $kind $revision $count)
     (observation $boot $id $observed-source $rev $reads $writes $lease) $request-id $operations)
  (let $batch (cons-atom ops $operations)
    (space:call $socket (request $request-id batch-observed $uuid $source $boot $id $batch) 5000)))

(: space:abort (-> String Atom %Undefined%))
(= (space:abort $socket $observation)
  (case $observation
    (((SpaceScopedObserved $handle $ticket $reads $writes $results)
       (space-scope:close $socket $ticket))
      ((SpaceTransaction $captured $key $fingerprint) (space:abort $socket $captured))
      ($other (SpaceRefused invalid-observation)))))

; Low-level descriptor. Until the service registers it, the caller still
; owns the observation. Prefer wait-start/poll/cancel below: cancelling only
; the I/O request before registration cannot release an unsent observation.
(: space:wait-request (-> String Atom Number %Undefined%))
(= (space:wait-request $socket $observation $duration)
  (case $observation
    (((SpaceScopedObserved (space $uuid $source $name $backend $kind $revision $count)
        (observation $boot $id $observed-source $rev $reads $writes $lease)
        $read-scopes $write-scopes $results)
       (let* (($rid (space:request-id)) ($deadline (+ $duration 5000)))
         (space:request $socket (request $rid observe-watch $uuid $source $boot $id $duration) $deadline)))
      ($other (SpaceRefused invalid-observation)))))

(: space:wait-start (-> String Atom Number %Undefined%))
(= (space:wait-start $socket $observation $duration)
  (let $request (space:wait-request $socket $observation $duration)
    (case $request
      (((space:request $endpoint $body $deadline)
         (case (io:try-submit $request)
           (((io:pending $id) (SpaceWaiting $observation $id))
             ($failed (let $closed (space-tx:release $socket $observation) $failed)))))
        ($failed $failed)))))

; A terminal event may mean the request was never sent. Retire the original
; observation too; if registration already consumed it, closing is harmless.
(: space:wait-poll (-> String Atom %Undefined%))
(= (space:wait-poll $socket (SpaceWaiting $observation $id))
  (case (io:poll $id)
    (((io:idle) (io:idle))
      ($terminal (let $closed (space-tx:release $socket $observation) $terminal)))))

(: space:wait-cancel (-> String Atom %Undefined%))
(= (space:wait-cancel $socket (SpaceWaiting $observation $id))
  (let* (($stopped (io:cancel $id)) ($closed (space-tx:release $socket $observation)))
    $stopped))

; Completed or refused begin operations must not retain scoped views. The
; conservative observation already closed its snapshots while reading.
(: space-tx:release (-> String Atom %Undefined%))
(= (space-tx:release $socket $observation)
  (case $observation
    (((SpaceScopedObserved $handle $ticket $reads $writes $results)
       (space-scope:close $socket $ticket))
      ($other True))))

(: space:begin (-> String Atom String String Atom Atom Number Number %Undefined%))

(= (space:begin $socket $handle $key $fingerprint $reads $writes $limit $lease)
  (let $receipt (SpaceReceiptV1 $key $prior-fingerprint $prior-result)
    (let $scope (scope SpaceReceiptSlot $receipt $receipt)
      (let $all-reads (cons-atom $scope $reads)
        (let $all-writes (cons-atom $receipt $writes)
          (let $observed (space:observe $socket $handle $all-reads $all-writes $limit $lease)
            (space-tx:begin-result $socket $observed $key $fingerprint)))))))

(: space:begin-scoped (-> String Atom String String Atom Atom Number Number %Undefined%))
(= (space:begin-scoped $socket $handle $key $fingerprint $reads $writes $limit $lease)
  (let* (($receipt (SpaceReceiptV1 $key $prior-fingerprint $prior-result))
         ($scope (scope SpaceReceiptSlot $receipt))
         ($all-reads (cons-atom $scope $reads)) ($all-writes (cons-atom $receipt $writes))
         ($observed (space:observe-scoped $socket $handle $all-reads $all-writes $limit $lease)))
    (space-tx:begin-result $socket $observed $key $fingerprint)))

(: space-tx:begin-result (-> String Atom String String %Undefined%))
(= (space-tx:begin-result $socket $observation $key $fingerprint)
  (case $observation
    (((SpaceObserved $current $reads $writes $results)
       (space-tx:begin-receipts $socket $observation $key $fingerprint $results))
      ((SpaceScopedObserved $current $ticket $reads $writes $results)
        (space-tx:begin-receipts $socket $observation $key $fingerprint $results))
      ($failed $failed))))

(: space-tx:begin-receipts (-> String Atom String String Atom %Undefined%))
(= (space-tx:begin-receipts $socket $observation $key $fingerprint $results)
  (let (SpaceRead SpaceReceiptSlot $receipts) (car-atom $results)
    (if (== $receipts ()) (SpaceTransaction $observation $key $fingerprint)
      (let $closed (space-tx:release $socket $observation)
        (if (== (size-atom $receipts) 1)
          (let (SpaceReceiptV1 $stored-key $stored-fingerprint $result) (car-atom $receipts)
            (if (== $stored-fingerprint $fingerprint) (SpaceCommitted $result)
              (SpaceRefused reused-operation-identity)))
          (SpaceRefused duplicate-application-receipt))))))

(: space-tx:receipt-value? (-> Atom %Undefined%))

(= (space-tx:receipt-value? $value)
  (if (== (get-metatype $value) Expression)
    (if (== $value ()) False (let $head (car-atom $value) (== $head SpaceReceiptV1))) False))

(: space-tx:application-operations (-> Atom %Undefined%))

(= (space-tx:application-operations $operations)
  (if (== $operations ()) True
    (let* (($operation (car-atom $operations)) ($rest (cdr-atom $operations)))
      (case $operation
        (((add $value)
           (if (space-tx:receipt-value? $value) False (space-tx:application-operations $rest)))
          ((remove $value)
            (if (space-tx:receipt-value? $value) False (space-tx:application-operations $rest)))
          ($invalid False))))))

(: space:publish (-> String Atom String Atom Atom %Undefined%))

(=
  (space:publish $socket (SpaceTransaction $observation $key $fingerprint) $request-id $result
    $operations)
  (if (space-tx:application-operations $operations)
    (let $receipt (cons-atom SpaceReceiptV1 ($key $fingerprint $result))
      (let $receipt-op (cons-atom add ($receipt))
        (let $all-operations (cons-atom $receipt-op $operations)
          (case (space:commit $socket $observation $request-id $all-operations)
            (((reply $rid ok $committed) (SpaceCommitted $result)) ($failed $failed))))))
    (let $closed (space-tx:release $socket $observation)
      (SpaceRefused reserved-receipt-mutation))))
