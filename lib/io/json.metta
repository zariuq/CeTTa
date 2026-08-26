!(import! &self io)
!(import! &self json)
!(import! &self io:rho)

; Preserve transport correlation and HTTP status while decoding the body
; through the authored JSON language.  Parse failures remain JsonFailureV1
; evidence inside the event rather than becoming transport failures.
(= (io:json:decode-event (io:event $id (http:response $status $body)))
   (io:json:event $id $status (json:parse $body)))

; A decoded event may enter the strict rho core on the same correlation
; channel as its raw I/O event.  Provider callbacks never reduce rho terms.
(= (io:json:event-process (io:json:event $id $status $value))
   (rho:send
     (io:rho:channel $id)
     (rho:val (io:json:event $id $status $value))))

(= (io:json:event-process
     (io:event $id (http:response $status $body)))
   (rho:send
     (io:rho:channel $id)
     (rho:val (io:json:event $id $status (json:parse $body)))))
