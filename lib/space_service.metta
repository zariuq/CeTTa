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

; Explicit grants stay reference-only: supplying one for an observation never
; installs it as the account's ambient/default grant.
(= (space:describe-authorized $socket (space $uuid $id $name $backend $kind $rev $count) $grant)
   (let $rid (space:request-id)
     (space:call $socket (request $rid describe $uuid $id (authority $grant)) 5000)))

(= (space:verify $socket (space $uuid $id $name $backend $kind $rev $count))
   (let $rid (space:request-id)
     (space:call $socket (request $rid verify $uuid $id) 5000)))

(= (space:verify-authorized $socket (space $uuid $id $name $backend $kind $rev $count) $grant)
   (let $rid (space:request-id)
     (space:call $socket (request $rid verify $uuid $id (authority $grant)) 5000)))

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

(= (space:query-authorized $socket (space $uuid $id $name $backend $kind $rev $count)
       $grant $pattern $template $limit $lease)
   (let $rid (space:request-id)
     (space:call $socket
       (request $rid query $uuid $id $pattern $template $limit $lease (authority $grant)) 5000)))

(= (space:count $socket (space $uuid $id $name $backend $kind $rev $count) $pattern)
   (let $rid (space:request-id)
     (space:call $socket (request $rid count $uuid $id $pattern) 5000)))

(= (space:count-authorized $socket (space $uuid $id $name $backend $kind $rev $count)
       $grant $pattern)
   (let $rid (space:request-id)
     (space:call $socket (request $rid count $uuid $id $pattern (authority $grant)) 5000)))

(= (space:watch $socket (space $uuid $id $name $backend $kind $rev $count) $revision $wait)
   (let $rid (space:request-id)
     (space:call $socket (request $rid watch $uuid $id $revision $wait) (+ $wait 5000))))

(= (space:watch-authorized $socket (space $uuid $id $name $backend $kind $rev $count)
       $grant $revision $wait)
   (let $rid (space:request-id)
     (space:call $socket
       (request $rid watch $uuid $id $revision $wait (authority $grant)) (+ $wait 5000))))

(= (space:fetch $socket (snapshot $boot $cursor $source $rev $total $lease) $offset $count)
   (let $rid (space:request-id)
     (space:call $socket (request $rid fetch $boot $cursor $offset $count) 5000)))

(= (space:close $socket (snapshot $boot $cursor $source $rev $total $lease))
   (let $rid (space:request-id)
     (space:call $socket (request $rid close $boot $cursor) 5000)))

(= (space:receipt $socket (space $uuid $id $name $backend $kind $rev $count) $request-id)
   (let $rid (space:request-id)
     (space:call $socket (request $rid receipt $uuid $request-id) 5000)))

; Resolve a service-stamped historical account origin. The reference proves no
; current authority and carries no agent identity within the authenticated OS
; account. Only that account and the service owner may inspect it.
(= (space:origin $socket (origin-ref $uuid $uid $request-id $nonce))
   (let $rid (space:request-id)
     (space:call $socket
       (request $rid origin $uuid (origin-ref $uuid $uid $request-id $nonce)) 5000)))

; The service names the connecting account from the kernel's credentials:
; (principal (account UID LOGIN) owner) or (principal (account UID LOGIN) member).
(= (space:whoami $socket)
   (let $rid (space:request-id)
     (space:call $socket (request $rid whoami) 5000)))

; The principal reference includes the service-issued incarnation. It is the
; identity carried by grants, tickets, retained reads and retry receipts.
(= (space:principal-ref $socket)
   (function
     (let $rid (space:request-id)
       (case (space:call $socket (request $rid hello) 5000)
         (((reply $hello-id ok (service $version $uuid $boot $catalog $caps))
           (let $principal-id (space:request-id)
             (case (space:call $socket
                     (request $principal-id principal-ref $uuid) 5000)
               (($principal (return $principal))))))
          ($other (return $other)))))))

; Owner-only rotation gives an account a fresh incarnation. Previously issued
; grants and retained work remain historical data but no longer authorize it.
(= (space:rotate-principal $socket $account)
   (function
     (let $rid (space:request-id)
       (case (space:call $socket (request $rid hello) 5000)
         (((reply $hello-id ok (service $version $uuid $boot $catalog $caps))
           (let $rotate-id (space:request-id)
             (case (space:call $socket
                     (request $rotate-id rotate-principal $uuid $account) 5000)
               (($rotated (return $rotated))))))
          ($other (return $other)))))))

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

; The ticket-rights subset still requires the ordinary grant and additionally
; requires one live service ticket for each successful mutation.
(= (space:grant-ticketed $socket (space $uuid $id $name $backend $kind $rev $count)
       $account $rights $ticket-rights $shape $unique)
   (let $rid (space:request-id)
     (space:call $socket
       (request $rid grant-ticketed $uuid $id $account $rights $ticket-rights $shape $unique) 5000)))

; Explicit root and delegated grants return a generation-bearing grant-ref.
; They never become the account's ambient/default grant. A delegated child
; inherits the parent's complete append form and can only narrow rights.
(= (space:grant-root $socket (space $uuid $id $name $backend $kind $rev $count)
       $account $rights $ticket-rights $delegable $shape $unique)
   (let $rid (space:request-id)
     (space:call $socket
       (request $rid grant-root $uuid $id $account $rights $ticket-rights
         $delegable $shape $unique) 5000)))

(= (space:delegate-grant $socket (space $uuid $id $name $backend $kind $rev $count)
       $parent $account $rights $ticket-rights $delegable)
   (let $rid (space:request-id)
     (space:call $socket
       (request $rid delegate-grant $uuid $id $parent $account $rights
         $ticket-rights $delegable) 5000)))

(= (space:revoke-grant $socket (space $uuid $id $name $backend $kind $rev $count) $grant)
   (let $rid (space:request-id)
     (space:call $socket (request $rid revoke-grant $uuid $id $grant) 5000)))

; A ticket is distinct from the grant that permits its use. The owner issues
; one ticket for append or write; the service consumes it only with a
; successful append-ticket or batch-ticket commit.
(= (space:issue-ticket $socket (space $uuid $id $name $backend $kind $rev $count)
       $account $right)
   (let $rid (space:request-id)
     (space:call $socket (request $rid issue-ticket $uuid $id $account $right) 5000)))

(= (space:issue-ticket-v2 $socket (space $uuid $id $name $backend $kind $rev $count)
       $grant $right)
   (let $rid (space:request-id)
     (space:call $socket (request $rid issue-ticket-v2 $uuid $id $grant $right) 5000)))

(= (space:append-ticket $socket (space $uuid $id $name $backend $kind $rev $count)
       $ticket $bundle)
   (let $rid (space:request-id)
     (space:call $socket (request $rid append-ticket $uuid $id $ticket $bundle) 5000)))

(= (space:batch-ticket $socket (space $uuid $id $name $backend $kind $rev $count)
       $ticket $ops)
   (let $rid (space:request-id)
     (space:call $socket (request $rid batch-ticket $uuid $id $rev $ticket $ops) 5000)))

(= (space:append-authorized $socket (space $uuid $id $name $backend $kind $rev $count)
       $grant $ticket-or-none $bundle)
   (let $rid (space:request-id)
     (space:call $socket
       (request $rid append-authorized $uuid $id $grant $ticket-or-none $bundle) 5000)))

(= (space:batch-authorized $socket (space $uuid $id $name $backend $kind $rev $count)
       $grant $ticket-or-none $ops)
   (let $rid (space:request-id)
     (space:call $socket
       (request $rid batch-authorized $uuid $id $rev $grant $ticket-or-none $ops) 5000)))

(= (space:revoke-ticket $socket (space $uuid $id $name $backend $kind $rev $count) $ticket)
   (let $rid (space:request-id)
     (space:call $socket (request $rid revoke-ticket $uuid $id $ticket) 5000)))
