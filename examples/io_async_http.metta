!(import! &self io)

; The host owns transport progress. MeTTa observes it only through io:poll,
; so evaluation can continue while one or more requests are pending.
(= (example:http-get $url $max-bytes)
   (io:submit
     (http:request "GET" $url () "" 30000 $max-bytes)))

(= (example:http-post $url $body $max-bytes)
   (io:submit
     (http:request "POST" $url
       ((http:header "Content-Type" "text/plain"))
       $body 30000 $max-bytes)))

(= (example:observe-one)
   (io:poll))

(= (example:cancel (io:pending $id))
   (io:cancel $id))

!(println! (io-example-capabilities (io:capabilities)))
