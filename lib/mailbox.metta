; A native mailbox client. All policy executes in the caller's MeTTa space.
; The service stores inert values and commits each transition atomically.
; A delivery stays ready until its owner acknowledges the committed turn.
!(import! &self space_service)
; Durable mailbox policy. Inputs and plans are portable data, not code.
; Native indexed observations come from one captured source revision; the
; host conditionally commits the complete plan against that same revision.
; Only pending records participate in receives. Completed message bodies
; remain once in the delivery ledger until an explicit retention decision.
(= (mailbox:append $left $right)
   (if (== $left ()) $right
     (cons-atom (car-atom $left) (mailbox:append (cdr-atom $left) $right))))

(= (mailbox:dispatch $request)
   (case $request
     (((send (MailboxMessage 1 $id $channel $correlation $sender $body) $existing)
       (case $existing
         ((() (MailboxPlan accepted
           ((add (MailboxMessage 1 $id $channel $correlation $sender $body))
            (add (MailboxPending 1 $channel $correlation $sender $id)))))
          (((MailboxMessage 1 $id $channel $correlation $sender $body))
           (MailboxPlan duplicate ()))
          ($ignored1 (MailboxFailure identity-conflict)))))
      ((register (MailboxWait 1 $id $channel $correlation $sender $owner $generation $continuation)
         $registration $owners $routes)
       (case $registration
         ((() (case ($owners $routes)
           (((() ()) (MailboxPlan waiting
             ((add (MailboxWait 1 $id $channel $correlation $sender $owner $generation $continuation))
              (add (MailboxRegistration 1 $id
                (MailboxWait 1 $id $channel $correlation $sender $owner $generation $continuation)))
              (add (MailboxOwner 1 $owner $generation $id))
              (add (MailboxRoute 1 $channel $correlation $sender $id)))))
            ($ignored2 (MailboxFailure occupied-owner-or-route)))))
          (((MailboxRegistration 1 $id
              (MailboxWait 1 $id $channel $correlation $sender $owner $generation $continuation)))
           (MailboxPlan duplicate ()))
          ($ignored3 (MailboxFailure wait-identity-conflict)))))
      ((register-intent
         (MailboxWait 1 $id $channel $turn $sender $owner $generation (MailboxResume 1 $tag $id))
         $registration $owners $routes
         (MailboxIntent 1 $id $tag (MailboxCodexWatch 1 $thread $turn)) $intents)
       (case (mailbox:dispatch (register
          (MailboxWait 1 $id $channel $turn $sender $owner $generation (MailboxResume 1 $tag $id))
          $registration $owners $routes))
         (((MailboxPlan $status $ops)
           (case $intents
             ((() (MailboxPlan $status (mailbox:append $ops
                 ((add (MailboxIntent 1 $id $tag (MailboxCodexWatch 1 $thread $turn)))))))
              (((MailboxIntent 1 $id $tag (MailboxCodexWatch 1 $thread $turn))) (MailboxPlan $status $ops))
              ($ignored4 (MailboxFailure intent-identity-conflict)))))
          ($failure $failure))))
      ((take (MailboxPending 1 $channel $correlation $sender $message)
         (MailboxMessage 1 $message $channel $correlation $sender $body) $owner)
       (MailboxPlan ready
         ((remove (MailboxPending 1 $channel $correlation $sender $message))
          (add (MailboxReady 1 $message $owner 0 () $message)))))
      ((receive (MailboxWait 1 $id $channel $correlation $sender $owner $generation $continuation)
         (MailboxPending 1 $channel $correlation $sender $message)
         (MailboxMessage 1 $message $channel $correlation $sender $body))
       (MailboxPlan ready
         ((remove (MailboxWait 1 $id $channel $correlation $sender $owner $generation $continuation))
          (remove (MailboxPending 1 $channel $correlation $sender $message))
          (add (MailboxReady 1 $id $owner $generation $continuation $message)))))
      ((listen (MailboxListener 1 $channel $sender $tag) $existing)
       (case $existing
         ((() (MailboxPlan listening ((add (MailboxListener 1 $channel $sender $tag)))))
          (((MailboxListener 1 $channel $sender $tag)) (MailboxPlan duplicate ()))
          ($ignored5 (MailboxFailure listener-conflict)))))
      ((accept (MailboxListener 1 $channel $sender $tag)
         (MailboxWait 1 $id $channel $correlation $sender $owner $generation (MailboxResume 1 $tag $id))
         (MailboxMessage 1 $message $channel $correlation $sender $body)
         $pending $registration $owners $deliveries
         ((MailboxListener 1 $channel $sender $tag)))
       (case ($registration $deliveries)
         (((() ())
           (case ($pending $owners)
             (((((MailboxPending 1 $channel $correlation $sender $message)) ())
               (MailboxPlan ready
                 ((remove (MailboxPending 1 $channel $correlation $sender $message))
                  (add (MailboxRegistration 1 $id
                    (MailboxWait 1 $id $channel $correlation $sender $owner $generation (MailboxResume 1 $tag $id))))
                  (add (MailboxOwner 1 $owner $generation $id))
                  (add (MailboxDelivery 1 $id $message))
                  (add (MailboxReady 1 $id $owner $generation (MailboxResume 1 $tag $id) $message)))))
              ($ignored6 (MailboxFailure unavailable-message-or-owner)))))
          ((((MailboxRegistration 1 $id
               (MailboxWait 1 $id $channel $correlation $sender $owner $generation (MailboxResume 1 $tag $id))))
            ((MailboxDelivery 1 $id $message))) (MailboxPlan duplicate ()))
          ($ignored7 (MailboxFailure delivery-identity-conflict)))))
      ((cancel (MailboxWait 1 $id $channel $correlation $sender $owner $generation $continuation)
         $owner $generation)
       (MailboxPlan cancelled
         ((remove (MailboxWait 1 $id $channel $correlation $sender $owner $generation $continuation))
          (add (MailboxCancelled 1 $id $owner $generation)))))
      ((settle (MailboxReady 1 $id $owner $generation $continuation $message) $owner $generation)
       (MailboxPlan settled
         ((remove (MailboxReady 1 $id $owner $generation $continuation $message))
          (add (MailboxSettled 1 $id $owner $generation $message)))))
      ((late (MailboxCancelled 1 $id $owner $generation)
         (MailboxRoute 1 $channel $correlation $sender $id)
         (MailboxPending 1 $channel $correlation $sender $message))
       (MailboxPlan late
         ((remove (MailboxPending 1 $channel $correlation $sender $message))
          (add (MailboxLate 1 $message $id $owner $generation)))))
      ($ignored8 (MailboxFailure invalid-transition)))))


(= (mailbox:open $socket $name)
   (function (case (case (space:open $socket $name native)
     (((reply $ignored9 ok $space) $space) ($error $error)))
     (($result (return $result))))))

(= (mailbox:at (space $uuid $id $name $backend $kind $ignored10 $count) $revision)
   (function (case (space $uuid $id $name $backend $kind $revision $count)
     (($result (return $result))))))

; An indexed first occurrence, with the revision used to select it. The
; temporary snapshot is closed even for an empty selection.
(= (mailbox:one $socket (space $uuid $id $name $backend $kind $rev $count) $pattern $template)
   (function (case (let $rid (space:request-id)
     (case (space:call $socket
             (request $rid query-first $uuid $id $pattern $template 1 30000) 5000)
       (((reply $ignored11 ok (selection $boot $cursor $source $revision $total $lease))
         (let $snapshot (snapshot $boot $cursor $source $revision $total $lease)
           (let $page (space:fetch $socket $snapshot 0 1)
             (let $closed (space:close $socket $snapshot)
               (case $page
                 (((reply $ignored12 ok (page $ignored13 $ignored14 $rows $ignored15 $ignored16))
                   (mailbox:found $revision (cdr-atom $rows)))
                  ($error $error)))))))
        ($error $error))))
     (($result (return $result))))))

(= (mailbox:commit $socket $space $ops)
   (function (case (case (space:batch $socket $space (cons-atom ops $ops))
     (((space:uncertain $rid $reason)
       (case (space:receipt $socket $space $rid)
         (((reply $ignored17 ok (reply $original ok $answer)) (reply $original ok $answer))
          ($other (space:uncertain $rid $reason)))))
      ($answer $answer)))
     (($result (return $result))))))

(= (mailbox:send $socket $name $id $to $sender $body)
   (function (case (let $space (mailbox:open $socket $name)
     (mailbox:send-in $socket $space $id $to $sender $body))
     (($result (return $result))))))
(= (mailbox:send-in $socket $space $id $to $sender $body)
   (function (case (case $space
     (((space $ignored18 $ignored19 $ignored20 $ignored21 $ignored22 $ignored23 $ignored24)
       (case (mailbox:one $socket $space (MailboxMessage 1 $id $c $q $s $b)
                         (MailboxMessage 1 $id $c $q $s $b))
         (((mailbox:found $revision $existing)
           (case (mailbox:dispatch (send (MailboxMessage 1 $id $to "" $sender $body) $existing))
             (((MailboxPlan duplicate ()) (mailbox:sent $id duplicate))
              ((MailboxPlan accepted $ops)
               (let $current (mailbox:at $space $revision)
                 (case (mailbox:commit $socket $current $ops)
                   (((reply $ignored25 ok $ignored26) (mailbox:sent $id accepted))
                    ((reply $ignored27 error stale-revision)
                     (mailbox:send-in $socket $space $id $to $sender $body))
                    ($error $error)))))
              ($error $error))))
          ($error $error))))
      ($error $error)))
     (($result (return $result))))))

; A send from another account is one append and reads nothing. The service
; binds (caller) in its grant's shape to the authenticated account, so that
; account cannot be forged; the agent name within an account is the caller's.
(= (mailbox:post $socket $name $id $to $agent $body)
   (function (case (case (space:whoami $socket)
     (((reply $ignored59 ok (principal $account $ignored60))
       (let $space (mailbox:open $socket $name)
         (case $space
           (((space $ignored61 $ignored62 $ignored63 $ignored64 $ignored65 $ignored66 $ignored67)
             (case (space:append $socket $space
                     (bundle (MailboxMessage 1 $id $to "" (from $account $agent) $body)
                             (MailboxPending 1 $to "" (from $account $agent) $id)))
               (((reply $ignored68 ok appended) (mailbox:sent $id accepted))
                ((reply $ignored69 ok duplicate) (mailbox:sent $id duplicate))
                ($error $error))))
            ($error $error)))))
      ($error $error)))
     (($result (return $result))))))

; The same authenticated bundle under an explicit commit-spent ticket.
(= (mailbox:post-ticket $socket $name $ticket $id $to $agent $body)
   (function (case (case (space:whoami $socket)
     (((reply $ignored70 ok (principal $account $ignored71))
       (let $space (mailbox:open $socket $name)
         (case $space
           (((space $ignored72 $ignored73 $ignored74 $ignored75 $ignored76 $ignored77 $ignored78)
             (case (space:append-ticket $socket $space $ticket
                     (bundle (MailboxMessage 1 $id $to "" (from $account $agent) $body)
                             (MailboxPending 1 $to "" (from $account $agent) $id)))
               (((reply $ignored79 ok appended) (mailbox:sent $id accepted))
                ((reply $ignored80 ok duplicate) (mailbox:sent $id duplicate))
                ($error $error))))
            ($error $error)))))
      ($error $error)))
     (($result (return $result))))))

(= (mailbox:next $socket $name $owner)
   (function (case (let $space (mailbox:open $socket $name)
     (mailbox:next-in $socket $space $owner))
     (($result (return $result))))))
(= (mailbox:next-in $socket $space $owner)
   (function (case (case $space
     (((space $ignored28 $ignored29 $ignored30 $ignored31 $ignored32 $ignored33 $ignored34)
       (case (mailbox:one $socket $space (MailboxReady 1 $id $owner 0 () $id) $id)
         (((mailbox:found $ignored35 ()) (mailbox:take-in $socket $space $owner))
          ((mailbox:found $ignored36 ($id)) (mailbox:body $socket $space $id $owner))
          ($error $error))))
      ($error $error)))
     (($result (return $result))))))
(= (mailbox:body $socket $space $id $owner)
   (function (case (case (mailbox:one $socket $space (MailboxMessage 1 $id $owner $q $sender $body)
                     (mailbox:delivery $id $sender $body))
     (((mailbox:found $ignored37 ($delivery)) $delivery)
      ((mailbox:found $ignored38 ()) (MailboxFailure missing-body))
      ($error $error)))
     (($result (return $result))))))
(= (mailbox:take-in $socket $space $owner)
   (function (case (case (mailbox:one $socket $space (MailboxPending 1 $owner $q $sender $id)
                     (MailboxPending 1 $owner $q $sender $id))
     (((mailbox:found $ignored39 ()) (mailbox:idle))
      ((mailbox:found $revision ($pending))
       (let (MailboxPending 1 $owner $q $sender $id) $pending
         (case (mailbox:one $socket $space (MailboxMessage 1 $id $owner $q $sender $body)
                           (MailboxMessage 1 $id $owner $q $sender $body))
           (((mailbox:found $body-revision ($message))
             (if (== $revision $body-revision)
               (case (mailbox:dispatch (take $pending $message $owner))
                 (((MailboxPlan ready $ops)
                   (let $current (mailbox:at $space $revision)
                     (case (mailbox:commit $socket $current $ops)
                       (((reply $ignored40 ok $ignored41) (mailbox:body $socket $space $id $owner))
                        ((reply $ignored42 error stale-revision) (mailbox:next-in $socket $space $owner))
                        ($error $error)))))
                  ($error $error)))
               (mailbox:next-in $socket $space $owner)))
            ((mailbox:found $ignored43 ()) (MailboxFailure missing-body))
            ($error $error)))))
      ($error $error)))
     (($result (return $result))))))

(= (mailbox:ack $socket $name $owner $id)
   (function (case (let $space (mailbox:open $socket $name)
     (mailbox:ack-in $socket $space $owner $id))
     (($result (return $result))))))
(= (mailbox:ack-in $socket $space $owner $id)
   (function (case (case $space
     (((space $ignored44 $ignored45 $ignored46 $ignored47 $ignored48 $ignored49 $ignored50)
       (case (mailbox:one $socket $space (MailboxReady 1 $id $owner 0 () $id)
                         (MailboxReady 1 $id $owner 0 () $id))
         (((mailbox:found $ignored51 ()) (mailbox:acked $id duplicate))
          ((mailbox:found $revision ($ready))
           (case (mailbox:dispatch (settle $ready $owner 0))
             (((MailboxPlan settled $ops)
               (let $current (mailbox:at $space $revision)
                 (case (mailbox:commit $socket $current $ops)
                   (((reply $ignored52 ok $ignored53) (mailbox:acked $id settled))
                    ((reply $ignored54 error stale-revision) (mailbox:ack-in $socket $space $owner $id))
                    ($error $error)))))
              ($error $error))))
          ($error $error))))
      ($error $error)))
     (($result (return $result))))))

; Watching is read-only. The revision check and registration are atomic on
; the store, so a send between a scan and this watch cannot be missed.
(= (mailbox:watch $socket (space $uuid $id $ignored55 $ignored56 $ignored57 $revision $ignored58) $milliseconds)
   (function (case (let $rid (space:request-id)
     (let $timeout (+ $milliseconds 1000)
       (space:call $socket (request $rid watch $uuid $id $revision $milliseconds) $timeout)))
     (($result (return $result))))))
