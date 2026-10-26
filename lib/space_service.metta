; Portable data RPC to a named-space service. Replies are inert values:
; (reply ID ok VALUE) or (reply ID error REASON). A lost reply after sending
; is (space:uncertain ID REASON); inspect the receipt before retrying writes.
; The service owns storage. A client needs neither MORK nor PathMap linkage.

; Observe public arguments before handing inert request data to the host.
(: space:call (-> String %Undefined% Number Expression))
(: __cetta_lib_space_call (-> Atom Atom Atom Atom))
(: space:request-id (-> String))

(= (space:call $socket $request $timeout)
   (function
     (chain (eval (__cetta_lib_space_call $socket $request $timeout))
       $result (return $result))))

(= (space:request-id)
   (function
     (chain (eval (__cetta_lib_space_id ())) $result (return $result))))

(= (space:list $socket)
   (function
     (let $rid (space:request-id)
       (case (space:call $socket (request $rid hello) 5000)
         (((reply $hello-id ok (service $version $uuid $boot $catalog $caps))
           (let $list-id (space:request-id)
             (case (space:call $socket (request $list-id list $uuid) 5000)
               (($listed (return $listed))))))
          ($other (return $other)))))))

(= (space:open $socket $name $backend)
   (function
     (let $rid (space:request-id)
       (case (space:call $socket (request $rid hello) 5000)
         (((reply $hello-id ok (service $version $uuid $boot $catalog $caps))
           (let $open-id (space:request-id)
             (case (space:call $socket
                     (request $open-id open $uuid $name $backend $catalog) 5000)
               (($opened (return $opened))))))
          ($other (return $other)))))))

(= (space:describe $socket (space $uuid $id $name $backend $kind $rev $count))
   (let $rid (space:request-id)
     (space:call $socket (request $rid describe $uuid $id) 5000)))

(= (space:verify $socket (space $uuid $id $name $backend $kind $rev $count))
   (let $rid (space:request-id)
     (space:call $socket (request $rid verify $uuid $id) 5000)))

(= (space:batch $socket (space $uuid $id $name $backend $kind $rev $count) $ops)
   (let $rid (space:request-id)
     (space:call $socket (request $rid batch $uuid $id $rev $ops) 5000)))

(= (space:add $socket $space $value)
   (space:batch $socket $space (ops (add $value))))

(= (space:remove $socket $space $value)
   (space:batch $socket $space (ops (remove $value))))

(= (space:query $socket (space $uuid $id $name $backend $kind $rev $count)
       $pattern $template $limit $lease)
   (let $rid (space:request-id)
     (space:call $socket (request $rid query $uuid $id $pattern $template $limit $lease) 5000)))

(= (space:count $socket (space $uuid $id $name $backend $kind $rev $count) $pattern)
   (let $rid (space:request-id)
     (space:call $socket (request $rid count $uuid $id $pattern) 5000)))

(= (space:fetch $socket (snapshot $boot $cursor $source $rev $total $lease) $offset $count)
   (let $rid (space:request-id)
     (space:call $socket (request $rid fetch $boot $cursor $offset $count) 5000)))

(= (space:close $socket (snapshot $boot $cursor $source $rev $total $lease))
   (let $rid (space:request-id)
     (space:call $socket (request $rid close $boot $cursor) 5000)))

(= (space:receipt $socket (space $uuid $id $name $backend $kind $rev $count) $request-id)
   (let $rid (space:request-id)
     (space:call $socket (request $rid receipt $uuid $request-id) 5000)))

; The service names the connecting account from the kernel's credentials:
; (principal (account UID LOGIN) owner) or (principal (account UID LOGIN) member).
(= (space:whoami $socket)
   (let $rid (space:request-id)
     (space:call $socket (request $rid whoami) 5000)))

; An append needs only the right to append: it names no revision and reads
; nothing. The values form one (bundle ...) committed whole, or not at all.
(= (space:append $socket (space $uuid $id $name $backend $kind $rev $count) $bundle)
   (let $rid (space:request-id)
     (space:call $socket (request $rid append $uuid $id $bundle) 5000)))

; The owner sets what one account may do on a store: (rights observe read
; append write), an optional (bundle ...) shape that its appends must match,
; with (caller) standing for its authenticated account, and an optional
; uniqueness pattern over the shape's variables. (rights) removes the grant.
(= (space:grant $socket (space $uuid $id $name $backend $kind $rev $count)
       $account $rights $shape $unique)
   (let $rid (space:request-id)
     (space:call $socket (request $rid grant $uuid $id $account $rights $shape $unique) 5000)))

; Conservative transactions. Reads and declared writes share one space and
; its revision. An empty read is a dependency too: ANY intervening mutation
; invalidates the commit. The service still checks the revision atomically.
; Reads are (scope LABEL PATTERN TEMPLATE); writes are matching patterns.
; LIMIT is the current snapshot resource allowance, never silent truncation.
; These helpers do not retry writes or turn an uncertain result into rollback.
; Applications must include their durable idempotency record in the same batch;
; the service's bounded request-reply cache is not that application record.
; Use the advertised native-ordered store profile when insertion order must
; survive removals. The older native profile promises bag multiplicity only.
(: space:observe (-> String Atom Atom Atom Number Number %Undefined%))
(= (space:observe $socket $handle $reads $writes $limit $lease)
   (function
     (case (space:describe $socket $handle)
       (((reply $rid ok $current)
          (case (space-tx:read-scopes $socket $current $reads $limit $lease ())
            (((SpaceReadResults $results)
               (return (SpaceObserved $current $reads $writes $results)))
             ($failed (return $failed)))))
        ($failed (return $failed))))))

(= (space-tx:revision (space $uuid $id $name $backend $kind $rev $count)) $rev)

(= (space-tx:read-scopes $socket $handle $remaining $limit $lease $results)
   (function
     (if (== $remaining ()) (return (SpaceReadResults $results))
       (case (car-atom $remaining)
         (((scope $label $pattern $template)
            (case (space:query $socket $handle $pattern $template $limit $lease)
              (((reply $rid ok $snapshot)
                 (case (space-tx:read-snapshot $socket $handle $snapshot)
                   (((SpaceRows $rows)
                      (let $next (union-atom $results ((SpaceRead $label $rows)))
                        (case (space-tx:read-scopes $socket $handle
                                (cdr-atom $remaining) $limit $lease $next)
                          (($answer (return $answer))))))
                    ($failed (return $failed)))))
               ($failed (return $failed)))))
          ($invalid (return (SpaceRefused invalid-read-scope))))))))

(= (space-tx:read-snapshot $socket $handle
       (snapshot $boot $cursor $source $rev $total $lease))
   (function
     (let* (($snapshot (snapshot $boot $cursor $source $rev $total $lease))
             ($answer (if (== $rev (space-tx:revision $handle))
                        (space-tx:pages $socket $snapshot 0 128 ())
                        (SpaceRefused stale-observation)))
             ($closed (space:close $socket $snapshot)))
       (return $answer))))

(= (space-tx:pages $socket $snapshot $offset $count $accumulated)
   (function
     (case (space:fetch $socket $snapshot $offset $count)
       (((reply $rid ok (page $rev $start $rows $next $done))
          (let* (($page-rows (cdr-atom $rows))
                  ($all (union-atom $accumulated $page-rows)))
            (if $done (return (SpaceRows $all))
              (case (space-tx:pages $socket $snapshot $next $count $all)
                (($answer (return $answer)))))))
        ((reply $rid error page-byte-limit)
          (if (> $count 1)
            (case (space-tx:pages $socket $snapshot $offset
                    (div $count 2) $accumulated)
              (($answer (return $answer))))
            (return (reply $rid error page-byte-limit))))
        ($failed (return $failed))))))

; Match data without evaluating a row as a program. Bindings are local to
; this boolean question and never escape into the caller's operation list.
(: space-tx:covered (-> Atom Atom %Undefined%))
(= (space-tx:covered $patterns $value)
   (if (== $patterns ()) False
     (let* (($pattern (car-atom $patterns)) ($rest (cdr-atom $patterns)))
       (let $matches (collapse (case $value (($pattern True) ($other False))))
         (if (== $matches (True)) True
           (space-tx:covered $rest $value))))))

(: space-tx:operations-covered (-> Atom Atom %Undefined%))
(= (space-tx:operations-covered $patterns $operations)
   (if (== $operations ()) True
     (let* (($operation (car-atom $operations)) ($rest (cdr-atom $operations)))
       (case $operation
         (((add $value)
            (if (space-tx:covered $patterns $value)
              (space-tx:operations-covered $patterns $rest) False))
          ((remove $value)
            (if (space-tx:covered $patterns $value)
              (space-tx:operations-covered $patterns $rest) False))
          ($invalid False))))))

(: space:commit (-> String Atom String Atom %Undefined%))
(= (space:commit $socket
       (SpaceObserved (space $uuid $id $name $backend $kind $rev $count)
         $reads $writes $results) $request-id $operations)
   (if (space-tx:operations-covered $writes $operations)
     (let $batch (cons-atom ops $operations)
       (space:call $socket (request $request-id batch $uuid $id $rev $batch) 5000))
     (SpaceRefused undeclared-write)))
