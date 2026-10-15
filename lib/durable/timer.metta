; Pure timer requests: $now comes from the host's recorded clock observation.
; The grant selects native timer.after authority on the current decision.
(= (durable:timer:after $grant $now $delay $value $reply)
   (let $due (+ $now $delay)
     (host:send $grant (timer:at 1 $due 0 FireOnce 0 $value) $reply)))

(= (durable:timer:every $grant $now $delay $period $policy $grace $value $reply)
   (let $due (+ $now $delay)
     (host:send $grant (timer:at 1 $due $period $policy $grace $value) $reply)))
