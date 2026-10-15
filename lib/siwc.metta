; SPDX-License-Identifier: MIT
; Native Sign in with ChatGPT. Token exchange, verification, protected files
; and rotating refresh stay in C. Only public metadata crosses this boundary.
!(import! &self io)
!(import! &self llm)

(: siwc:request (-> String String Number Number Atom))
(= (siwc:sign-in $registration $port $deadline-ms)
   (io:submit (siwc:request "sign-in" $registration $port $deadline-ms)))
(= (siwc:ensure $registration $deadline-ms)
   (io:submit (siwc:request "ensure" $registration 0 $deadline-ms)))
(= (siwc:sign-out $registration $deadline-ms)
   (io:submit (siwc:request "sign-out" $registration 0 $deadline-ms)))
(= (siwc:accounts)
   (io:submit (siwc:request "accounts" "" 0 0)))

; Wait on this handle only; other completions stay queued. A zero wait is a
; normal poll, not a cancellation. The caller chooses the waiting policy.
(= (siwc:observe $id $wait-ms)
   (let $event (io:wait $id $wait-ms)
    (case $event
     (((io:event $id (siwc:result $json)) (llm:v1:parse $json))
      ($other $other)))))
(= (siwc:status $id)
   (let $status (io:status $id)
    (case $status
     (((siwc:progress $json) (llm:v1:parse $json))
      ($other $other)))))
