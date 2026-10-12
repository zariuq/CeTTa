!(import! &self durable:telegram_agent)
!(import! &self durable:telegram_commands)
!(import! &self durable:timer)

; Fixed service program, telegram-channel/1. The client is an agent's own
; long-lived loop: it reads deliveries and receipts as worker tasks, answers
; each task only to acknowledge it, and acts through keyed submissions. The
; per-chat sending state machine is telegram-agent/1's. Client text is data,
; never code, a destination, a grant or a scope.
;
; Every observation is a JSON array built from lane IDs, fixed kinds, roles,
; numbers and exact serialized provider objects, none of which need escaping:
;   ["input", lane, kind, role, update]
;   ["input-too-large", lane, kind, role]
;   ["delivery", key, position, count, result]
;   ["rejected", key, reason]
;   ["released", lane, key, position]
;   ["command", key, name, arguments, lane]
;   ["callback", key, data, lane, message]
; with result ["delivered", message-id] | ["failed", code] |
; ["not-sent"] | ["uncertain", reason].

(= (tg-channel:limit) 65536)

; Actor records carry the version of the program that wrote them. A chat
; actor written by any other program version is held, never reinterpreted.
(= (tg-channel:state)
   (case (collapse (tg-host:match (host:record 1 $key $record) $record))
     ((() (tg-agent:idle))
      (((host:actor 1 "telegram-channel/1" $state)) $state)
      ($other (tg-agent:held invalid-actor)))))

; Dispatch query: view 0 = accepted intent, 1 = current actor, 2 = the
; lifecycle latch. Only the expected position is eligible, and nothing while
; stopped; native admission also checks attempts/outcomes.
(= (tg-channel:dispatchable)
   (tg-host:match (host:record 0 $key (host:intent 1 "telegram.action" "1" $action
     (tg-agent:sent 1 $source $lane $batch $position $count)))
     (if (tg-cmd:stopped 2) False
       (case (tg-channel:state)
         (((tg-agent:sending $batch $position $count) True)
          ($other False))))))

; Incoming input: views 2 = received record, 3 = this update's command
; record (grant 1), 4 = the lifecycle latch (grant 2). Channels: 0 = the
; client worker, 1 = Telegram actions, 2 = timers. The chat's sending actor is
; not involved: reading never makes a draft stale and never waits for
; cognition. Non-routed input uses the audit actor. $now is the host's clock
; sample for this decision; $deadline is the command deadline in ms.
(= (tg-channel:receive $worker $source $lane $now $deadline)
   (tg-host:match (host:record 0 $key (host:input 1 $source $id $ledger $lane $disposition))
     (tg-host:match (host:record 2 $ledger (host:received 1 $source $id $lane $disposition $input))
       (if (== $disposition routed)
         (case $input
           (((telegram:input 1 $kind $chat $thread $sender $role $update)
             (if (== $kind "callback_query")
               (tg-channel:tap $worker $source $id $chat $thread $role $update $now $deadline)
               (case (tg-cmd:of $kind $role $update)
                 (((tg-cmd:call $name $command-kind $args)
                   (tg-channel:command $worker $source $id $chat $thread $update $name $command-kind $args $now $deadline))
                  ($ordinary (tg-channel:deliver $worker $source $id $lane $kind $role $update))))))
            ($bad Empty)))
         (host:transition (tg-agent:ignored $source $id $disposition) () ())))))

(= (tg-channel:deliver $worker $source $id $lane $kind $role $update)
   (let $update-json (json:stringify $update)
     (let $full (format-args "[\"input\",\"{}\",\"{}\",\"{}\",{}]" ($lane $kind $role $update-json))
       (let $observation
            (if (> (str:byte-length $full) (tg-channel:limit))
              (format-args "[\"input-too-large\",\"{}\",\"{}\",\"{}\"]" ($lane $kind $role))
              $full)
         (host:transition (tg-channel:intake) ()
           ((durable:worker:request 0 $worker $observation
              (tg-channel:delivered 1 $source $lane $id))))))))

; An operator command, keyed by its update. A reply quotes the command
; message and never waits for the chat's lane or for the agent.
(= (tg-channel:command-reply $source $key $chat $thread $message $text $role)
   (host:send 1 (telegram:send-text 1 $chat $thread $message $text "plain")
     (tg-channel:command-sent 1 $source $key $role)))

(= (tg-channel:command $worker $source $id $chat $thread $update $name $kind $args $now $deadline)
   (let $key (format-args "{}" ($id))
     (let $message (case (telegram:field $update "message")
                     (((tg:some $m) (case (telegram:optional-id $m "message_id" 0)
                                      (((tg:integer $n) $n) ($bad 0))))
                      ($none 0)))
       (let $ask (durable:worker:request 0 $worker
                   (tg-cmd:observation $key $name $args (format-args "{}.{}" ($chat $thread)))
                   (tg-channel:asked 1 $source $key))
         (case $kind
           ((help
             (host:transition (tg-channel:intake)
               ((host:put 1 $key (tg-cmd:served $now)))
               ((tg-channel:command-reply $source $key $chat $thread $message (tg-cmd:help-text) reply))))
            (stop
             (host:transition (tg-channel:intake)
               ((host:put 1 $key (tg-cmd:waiting $now $chat $thread $message)) (host:put 2 "latch" (tg-cmd:latch stopped)))
               ($ask (durable:timer:after 2 $now $deadline $key (tg-channel:deadline 1 $source $key)))))
            (start
             (host:transition (tg-channel:intake)
               ((host:put 1 $key (tg-cmd:waiting $now $chat $thread $message)) (host:put 2 "latch" (tg-cmd:latch running)))
               ($ask (durable:timer:after 2 $now $deadline $key (tg-channel:deadline 1 $source $key)))))
            (delegated
             (host:transition (tg-channel:intake)
               ((host:put 1 $key (tg-cmd:waiting $now $chat $thread $message)))
               ($ask (durable:timer:after 2 $now $deadline $key (tg-channel:deadline 1 $source $key)))))))))))

; A tap on a button of a menu, keyed by its update. An operator's tap is asked
; of the agent and races the deadline like a delegated command: before it,
; the answer both answers the tap and redraws the menu; after it, the service
; has answered the tap and the answer only redraws the menu. Anyone else's tap
; is answered at once. A tap never reaches the agent as input, and a
; malformed one is ignored.
(= (tg-channel:tap $worker $source $id $chat $thread $role $update $now $deadline)
   (let $key (format-args "{}" ($id))
     (case (tg-cmd:tap $update)
       (((tg:some (tg-cmd:tap $query $data $message))
         (if (== $role operator)
           (host:transition (tg-channel:intake)
             ((host:put 1 $key (tg-cmd:tapped $now $chat $message $query)))
             ((durable:worker:request 0 $worker
                (tg-cmd:tap-observation $key $data (format-args "{}.{}" ($chat $thread)) $message)
                (tg-channel:asked 1 $source $key))
              (durable:timer:after 2 $now $deadline $key (tg-channel:deadline 1 $source $key))))
           (host:transition (tg-channel:intake)
             ((host:put 1 $key (tg-cmd:served $now)))
             ((host:send 1 (telegram:answer-callback 1 $chat $query (tg-cmd:operator-only-text))
                (tg-channel:command-sent 1 $source $key refusal))))))
        ($none (host:transition (tg-channel:intake) () ()))))))

; The client answered a delivery or receipt task. Any body only acknowledges
; it: actions are never taken from an acknowledgment.
(= (tg-channel:ack $worker)
   (tg-host:match (host:record 0 $key (host:worker-result 1 $worker $task $revision $body))
     (host:transition (tg-channel:intake) () ())))

; A keyed submission for one lane. Views 2 = the writable decision key
; (grant 0), 3 = this lane's pending submissions, 4 = the lifecycle latch:
; while stopped, a submission is rejected. Channels: 0 = Telegram actions,
; 1 = the client worker. The native host passes whether this is the lane's
; oldest pending submission in the ticket's own snapshot; a younger one waits.
; Destinations come from the key's lane, never from the client's JSON.
(= (tg-channel:submit $worker $source $lane $chat $thread $oldest)
   (tg-host:match (host:record 0 $input (host:worker-submission 1 $worker $key $body))
     (if (not $oldest) Empty
       (case (tg-channel:state)
         (((tg-agent:idle)
           (if (tg-cmd:stopped 4) (tg-channel:reject $worker $source $key stopped)
           (case (tg-agent:batch $body $chat $thread)
             (((tg-agent:actions $actions)
               (let $count (size-atom $actions)
                 (host:transition (if (== $count 0) (tg-agent:idle) (tg-agent:sending $key 0 $count))
                   ((host:put 0 $key (tg-channel:decision 1 accepted $count)))
                   (tg-agent:intents $actions $source $lane $key 0 $count))))
              ($other (tg-channel:reject $worker $source $key invalid-command-batch))))))
          ($busy Empty))))))

(= (tg-channel:reject $worker $source $key $reason)
   (host:transition (tg-agent:idle)
     ((host:put 0 $key (tg-channel:decision 1 rejected $reason)))
     ((durable:worker:request 1 $worker (format-args "[\"rejected\",\"{}\",\"{}\"]" ($key $reason))
        (tg-channel:notified 1 $source $key)))))

; The host selects this branch only after a complete proposal fails its
; immutable action permissions. It uses the same observation and grants.
(= (tg-channel:reject-invalid-submission $worker $source)
   (tg-host:match (host:record 0 $input (host:worker-submission 1 $worker $key $body))
     (tg-channel:reject $worker $source $key action-not-permitted)))

(= (tg-channel:result-json $result)
   (case $result
     (((tg-agent:delivered $id) (format-args "[\"delivered\",{}]" ($id)))
      ((tg-agent:failed $code) (format-args "[\"failed\",{}]" ($code)))
      ((tg-agent:not-sent) "[\"not-sent\"]")
      ((tg-agent:uncertain $reason) (format-args "[\"uncertain\",\"{}\"]" ($reason)))
      ($other "[\"uncertain\",\"unclassified\"]"))))

; Effects never reuse a connection. A fresh connection that could not
; resolve the host (6), connect (7), complete TLS (35) or verify the peer
; (60), with no request byte issued, never carried the request: that is
; not-sent, as certain as a transfer that never started. Timeouts and later
; failures stay uncertain.
(= (tg-channel:classify $outcome $action)
   (case $outcome
     (((host:outcome 1 $effect $attempt uncertain (True False $code $status True 0 False False False) $body)
       (if (telegram:contains $code (6 7 35 60)) (tg-agent:not-sent) (tg-agent:outcome $outcome $action)))
      ($other (tg-agent:outcome $outcome $action)))))

; Every terminal observation advances the lane. An uncertain message keeps
; its own outcome and is never retried; later messages can still be sent.
(= (tg-channel:advances $result) True)

; Completion views: 2 original intent, 3 outcome, 4 writable delivery key
; (grant 2). Channel 0 = the client worker, for the receipt only.
(= (tg-channel:complete $worker)
   (tg-host:match (host:record 0 $input (host:completion 1 $effect $outkey))
    (tg-host:match (host:record 2 $outkey (host:intent 1 "telegram.action" "1" $action
      (tg-agent:sent 1 $source $lane $batch $position $count)))
     (tg-host:match (host:record 3 $outkey (host:outcome 1 $effect $attempt $kind $metadata $body))
      (case (tg-channel:state)
       (((tg-agent:sending $batch $position $count)
         (let $result (tg-channel:classify (host:outcome 1 $effect $attempt $kind $metadata $body) $action)
           (let $result-json (tg-channel:result-json $result)
            (let $receipt (format-args "[\"delivery\",\"{}\",{},{},{}]"
                            ($batch $position $count $result-json))
             (host:transition
               (if (tg-channel:advances $result)
                 (if (== (+ $position 1) $count) (tg-agent:idle) (tg-agent:sending $batch (+ $position 1) $count))
                 (tg-agent:held $batch $position $count $result))
               ((host:put 2 $outkey (tg-agent:delivery 1 $batch $position $result)))
               ((durable:worker:request 0 $worker $receipt
                  (tg-channel:notified 1 $source $outkey))))))))
        ($other Empty)))))))

; An operator releases a lane held by uncertain delivery. Views 2 = the
; immutable request, 3 = its result key (grant 1). Channel 0 = the client,
; for the notice only. The uncertain action stays uncertain; the rest of its
; batch is abandoned and the lane takes the next submission. The client is
; told which action it was, so it can say so rather than assume either way.
(= (tg-channel:control $worker)
   (tg-host:match (host:record 0 $input (host:telegram-control 1 $source $id $lane "release-worker" $batch))
     (tg-host:match (host:record 2 $id (host:telegram-control 1 $source $id $lane "release-worker" $batch))
       (let $state (tg-channel:state)
         (if (or (== $state (tg-agent:held invalid-actor))
                 (not (== (collapse (tg-host:match (host:record 3 $id $prior) True)) ()))) Empty
           (case $state
             (((tg-agent:held $batch $position $count $result)
               (let $notice (format-args "[\"released\",\"{}\",\"{}\",{}]" ($lane $batch $position))
                 (host:transition (tg-agent:idle)
                   ((host:put 1 $id (tg-agent:control-result 1 released $batch)))
                   ((durable:worker:request 0 $worker $notice (tg-channel:notified 1 $source $id))))))
              ($other (host:transition $state
                 ((host:put 1 $id (tg-agent:control-result 1 refused $batch))) ())))))))))

; The agent answered a command or a tap. Views 2 task, 3 origin, 4 the
; request, 5 the record (grant 3). Channel 0 = Telegram actions. Before the
; deadline the answer is the reply; after it, the answer edits the notice, at
; once if the notice was delivered, else when it is. For a tap, the answer
; answers the tap and redraws its menu before the deadline, and only redraws
; it after. Any other state, or a body that is neither an answer nor a menu,
; only acknowledges the task.
(= (tg-channel:answer $worker)
   (tg-host:match (host:record 0 $input (host:worker-result 1 $worker $task $revision $body))
     (tg-host:match (host:record 4 $outkey (host:intent 1 "worker.request" "1" $request
                    (tg-channel:asked 1 $source $key)))
       (let $phase (collapse (tg-host:match (host:record 5 $key $value) $value))
         (case ((tg-cmd:reply $body) $phase)
           ((((tg:some (tg-cmd:reply $toast $text $keyboard)) ((tg-cmd:waiting $t0 $chat $thread $message)))
             (host:transition (tg-channel:command)
               ((host:put 3 $key (tg-cmd:answered $t0)))
               ((host:send 0 (tg-cmd:send $chat $thread $message $text $keyboard)
                  (tg-channel:command-sent 1 $source $key reply)))))
            (((tg:some (tg-cmd:reply $toast $text $keyboard)) ((tg-cmd:fell-back $t0 $chat $thread $message $sent)))
             (if (> $sent 0)
               (host:transition (tg-channel:command)
                 ((host:put 3 $key (tg-cmd:edited $t0)))
                 ((host:send 0 (tg-cmd:edit $chat $sent $text $keyboard)
                    (tg-channel:command-sent 1 $source $key edit))))
               (host:transition (tg-channel:command)
                 ((host:put 3 $key (tg-cmd:edit-pending $t0 $chat $thread $message $text $keyboard))) ())))
            (((tg:some (tg-cmd:reply $toast $text $keyboard)) ((tg-cmd:tapped $t0 $chat $message $query)))
             (host:transition (tg-channel:command)
               ((host:put 3 $key (tg-cmd:answered $t0)))
               ((host:send 0 (telegram:answer-callback 1 $chat $query $toast)
                  (tg-channel:command-sent 1 $source $key acknowledge))
                (host:send 0 (tg-cmd:edit $chat $message $text $keyboard)
                  (tg-channel:command-sent 1 $source $key redraw)))))
            (((tg:some (tg-cmd:reply $toast $text $keyboard)) ((tg-cmd:tap-late $t0 $chat $message)))
             (host:transition (tg-channel:command)
               ((host:put 3 $key (tg-cmd:edited $t0)))
               ((host:send 0 (tg-cmd:edit $chat $message $text $keyboard)
                  (tg-channel:command-sent 1 $source $key redraw)))))
            ($other (host:transition (tg-channel:command) () ()))))))))

; The deadline of a command or a tap. View 2 = its record (grant 0).
; Channel 0 = Telegram actions. Only a command still waiting gets the notice,
; and only a tap still waiting is answered.
(= (tg-channel:deadline $source $key)
   (case (collapse (tg-host:match (host:record 2 $key $value) $value))
     ((((tg-cmd:waiting $t0 $chat $thread $message))
       (host:transition (tg-channel:command)
         ((host:put 0 $key (tg-cmd:fell-back $t0 $chat $thread $message 0)))
         ((host:send 0 (telegram:send-text 1 $chat $thread $message (tg-cmd:fallback-text) "plain")
            (tg-channel:command-sent 1 $source $key fallback)))))
      (((tg-cmd:tapped $t0 $chat $message $query))
       (host:transition (tg-channel:command)
         ((host:put 0 $key (tg-cmd:tap-late $t0 $chat $message)))
         ((host:send 0 (telegram:answer-callback 1 $chat $query (tg-cmd:tap-fallback-text))
            (tg-channel:command-sent 1 $source $key late)))))
      ($other (host:transition (tg-channel:command) () ())))))

; A command message was sent. Views 2 its intent, 3 its outcome, 4 the
; command's record (grant 2). Channel 0 = Telegram actions. A delivered
; notice records its message, and an answer already waiting edits it. A
; notice that was not delivered can never be edited.
(= (tg-channel:command-complete)
   (tg-host:match (host:record 0 $input (host:completion 1 $effect $outkey))
    (tg-host:match (host:record 2 $outkey (host:intent 1 "telegram.action" "1" $action
                   (tg-channel:command-sent 1 $source $key $role)))
     (tg-host:match (host:record 3 $outkey (host:outcome 1 $effect $attempt $kind $metadata $body))
      (if (not (== $role fallback)) (host:transition (tg-channel:command) () ())
       (let $result (tg-channel:classify (host:outcome 1 $effect $attempt $kind $metadata $body) $action)
        (case ($result (collapse (tg-host:match (host:record 4 $key $value) $value)))
          ((((tg-agent:delivered $sent) ((tg-cmd:fell-back $t0 $chat $thread $message 0)))
            (host:transition (tg-channel:command)
              ((host:put 2 $key (tg-cmd:fell-back $t0 $chat $thread $message $sent))) ()))
           (((tg-agent:delivered $sent) ((tg-cmd:edit-pending $t0 $chat $thread $message $text $keyboard)))
            (host:transition (tg-channel:command)
              ((host:put 2 $key (tg-cmd:edited $t0)))
              ((host:send 0 (tg-cmd:edit $chat $sent $text $keyboard)
                 (tg-channel:command-sent 1 $source $key edit)))))
           (((tg-agent:delivered $sent) ((tg-cmd:edit-pending $t0 $chat $thread $message $text)))
            (host:transition (tg-channel:command)
              ((host:put 2 $key (tg-cmd:edited $t0)))
              ((host:send 0 (telegram:edit-text 1 $chat $sent $text "plain")
                 (tg-channel:command-sent 1 $source $key edit)))))
           (($undelivered ((tg-cmd:fell-back $t0 $chat $thread $message 0)))
            (host:transition (tg-channel:command) ((host:put 2 $key (tg-cmd:undelivered $t0))) ()))
           (($undelivered ((tg-cmd:edit-pending $t0 $chat $thread $message $text $keyboard)))
            (host:transition (tg-channel:command) ((host:put 2 $key (tg-cmd:undelivered $t0))) ()))
           (($undelivered ((tg-cmd:edit-pending $t0 $chat $thread $message $text)))
            (host:transition (tg-channel:command) ((host:put 2 $key (tg-cmd:undelivered $t0))) ()))
           ($other (host:transition (tg-channel:command) () ()))))))))))
