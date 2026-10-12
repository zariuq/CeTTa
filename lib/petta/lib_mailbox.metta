; Durable mailbox policy. Inputs and plans are portable data, not code.
; Native indexed observations come from one captured source revision; the
; host conditionally commits the complete plan against that same revision.
; Only pending records participate in receives. Completed message bodies
; remain once in the delivery ledger until an explicit retention decision.
(= (mailbox:dispatch $request)
   (case $request
     (((send (MailboxMessage 1 $id $channel $correlation $sender $body) $existing)
       (case $existing
         ((() (MailboxPlan accepted
           ((add (MailboxMessage 1 $id $channel $correlation $sender $body))
            (add (MailboxPending 1 $channel $correlation $sender $id)))))
          (((MailboxMessage 1 $id $channel $correlation $sender $body))
           (MailboxPlan duplicate ()))
          ($_ (MailboxFailure identity-conflict)))))
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
            ($_ (MailboxFailure occupied-owner-or-route)))))
          (((MailboxRegistration 1 $id
              (MailboxWait 1 $id $channel $correlation $sender $owner $generation $continuation)))
           (MailboxPlan duplicate ()))
          ($_ (MailboxFailure wait-identity-conflict)))))
      ((receive (MailboxWait 1 $id $channel $correlation $sender $owner $generation $continuation)
         (MailboxPending 1 $channel $correlation $sender $message)
         (MailboxMessage 1 $message $channel $correlation $sender $body))
       (MailboxPlan ready
         ((remove (MailboxWait 1 $id $channel $correlation $sender $owner $generation $continuation))
          (remove (MailboxPending 1 $channel $correlation $sender $message))
          (add (MailboxReady 1 $id $owner $generation $continuation $message)))))
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
      ($_ (MailboxFailure invalid-transition)))))
