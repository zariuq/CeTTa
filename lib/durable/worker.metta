!(import! &self durable:rho)

; The host chooses the worker channel grant. This is a proposal, not a call.
(= (durable:worker:request $grant $worker $observation $reply)
   (durable:rho:request $grant (worker:request 1 $worker $observation) $reply))
