!(import! &self io)
!(import! &self rho)

; Pure bridge from observable I/O envelopes to strict-core rho terms.
; Provider callbacks remain outside rho reduction: an evaluator turn polls an
; event, then this projection makes its correlation identity and outcome
; available as an ordinary rho communication.

(: io:rho:channel (-> Number Atom))
(: io:rho:pending-channel (-> Atom Atom))
(: io:rho:event-process (-> Atom Atom))

(= (io:rho:channel $id)
   (rho:quote (rho:val (io:request $id))))

(= (io:rho:pending-channel (io:pending $id))
   (rho:quote (rho:val (io:request $id))))

(= (io:rho:event-process (io:event $id $outcome))
   (rho:send
     (rho:quote (rho:val (io:request $id)))
     (rho:val (io:event $id $outcome))))
