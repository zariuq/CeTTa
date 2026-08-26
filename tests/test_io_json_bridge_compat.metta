!(import! &self io:json)

!(io:json:decode-event
   (io:event 21 (http:response 200 "{\"kind\":\"message\"}")))
!(io:json:decode-event
   (io:event 22 (http:response 502 "01")))
