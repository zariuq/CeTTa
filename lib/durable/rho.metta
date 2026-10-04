!(import! &self rhometta)

; These are descriptions, not immediate external operations. Grant numbers
; select native authority on the current host decision; names alone grant none.
(= (durable:rho:request $grant $payload $reply)
   (host:send $grant $payload $reply))

; The host supplies this only after the observation is durable. Effect IDs
; are strings; transport handles and credentials never enter a rho name.
(= (durable:rho:channel $id)
   (rho:quote (rho:val (host:effect $id))))

(= (durable:rho:completion $id $fact)
   (rho:send (rho:quote (rho:val (host:effect $id))) (rho:val $fact)))
