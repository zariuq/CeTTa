!(import! &self io:rho)
!(import! &self rhometta)

!(io:rho:channel 7)
!(io:rho:pending-channel (io:pending 7))
!(io:rho:event-process (io:event 7 (http:response 200 "ok")))

; A non-event remains uninterpreted rather than being recast as a rho event.
!(io:rho:event-process (io:idle))

; The projected event communicates through the ordinary strict-core reducer.
!(collapse
  (rhometta:transitions
    (rho:par
      (rho:recv
        (io:rho:channel 7)
        $message
        (rho:drop $message))
      (io:rho:event-process
        (io:event 7 (http:response 200 "ok"))))))
