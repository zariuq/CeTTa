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
        (if (> $count 1) (space-tx:pages $socket $snapshot $offset (div $count 2) $accumulated)
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

(=
  (space:commit $socket
    (SpaceObserved (space $uuid $id $name $backend $kind $rev $count) $reads $writes $results)
    $request-id $operations)
  (if (space-tx:operations-covered $writes $operations)
    (let $batch (cons-atom ops $operations)
      (space:call $socket (request $request-id batch $uuid $id $rev $batch) 5000))
    (SpaceRefused undeclared-write)))

(: space:begin (-> String Atom String String Atom Atom Number Number %Undefined%))

(= (space:begin $socket $handle $key $fingerprint $reads $writes $limit $lease)
  (let $receipt (SpaceReceiptV1 $key $prior-fingerprint $prior-result)
    (let $scope (scope SpaceReceiptSlot $receipt $receipt)
      (let $all-reads (cons-atom $scope $reads)
        (let $all-writes (cons-atom $receipt $writes)
          (case (space:observe $socket $handle $all-reads $all-writes $limit $lease)
            (((SpaceObserved $current $read-scopes $write-scopes $results)
               (let $observation (SpaceObserved $current $read-scopes $write-scopes $results)
                 (let (SpaceRead SpaceReceiptSlot $receipts) (car-atom $results)
                   (if (== $receipts ()) (SpaceTransaction $observation $key $fingerprint)
                     (if (== (size-atom $receipts) 1)
                       (let (SpaceReceiptV1 $stored-key $stored-fingerprint $result)
                         (car-atom $receipts)
                         (if (== $stored-fingerprint $fingerprint) (SpaceCommitted $result)
                           (SpaceRefused reused-operation-identity)))
                       (SpaceRefused duplicate-application-receipt)))))) ($failed $failed))))))))

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
    (SpaceRefused reserved-receipt-mutation)))
