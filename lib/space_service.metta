; Portable data RPC to a named-space service. Replies are inert values:
; (reply ID ok VALUE) or (reply ID error REASON). A lost reply after sending
; is (space:uncertain ID REASON); inspect the receipt before retrying writes.
; The service owns storage. A client needs neither MORK nor PathMap linkage.

; Resolve the socket and deadline; the request is inert syntax. Evaluate
; request-building computations into values before calling this boundary.
(: space:call (-> String Atom Number Expression))
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
