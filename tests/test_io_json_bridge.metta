!(import! &self io:json)
!(import! &self rhometta)

!(io:json:decode-event
   (io:event 11 (http:response 200 "{\"x\":1,\"x\":2}")))

; A JSON failure remains correlated with the successful HTTP response.
!(io:json:decode-event
   (io:event 12 (http:response 200 "01")))

; A non-HTTP event remains uninterpreted.
!(io:json:decode-event (io:event 13 (io:cancelled)))

!(collapse
  (rhometta:transitions
    (rho:par
      (rho:recv
        (io:rho:channel 11)
        $message
        (rho:drop $message))
      (io:json:event-process
        (io:event 11 (http:response 200 "{\"ok\":true}"))))))
