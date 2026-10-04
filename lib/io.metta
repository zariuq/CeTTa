; Portable, nonblocking host-I/O boundary.
;
; A provider may finish work between evaluator turns, but completion enters
; MeTTa only through io:poll or the native blocking io:wait. Requests receive
; host-assigned correlation IDs; each completion is observed at most once.

(: io:capabilities (-> Expression))
(: io:submit (-> Atom Expression))
(: io:poll (-> Expression))
(: io:wait (-> Number Expression))
(: io:cancel (-> Number Expression))

(: http:request (-> Atom Atom Expression String Number Number Atom))
(: http:request (-> Atom Atom Expression String Number Number Bool Atom))
(: http:header (-> String String Atom))
(: http:response (-> Number String Atom))

(= (io:capabilities)
   (__cetta_lib_io_capabilities ()))

(= (io:submit $request)
   (__cetta_lib_io_submit $request))

(= (io:poll)
   (__cetta_lib_io_poll ()))

(= (io:cancel $id)
   (__cetta_lib_io_cancel $id))

(= (http:get $url)
   (io:submit (http:request "GET" $url () "" 30000 8388608)))

(= (http:get $url $headers)
   (io:submit (http:request "GET" $url $headers "" 30000 8388608)))

(= (http:post $url $headers $body)
   (io:submit (http:request "POST" $url $headers $body 60000 8388608)))

(= (io:wait $timeout-ms)
   (__cetta_lib_io_wait $timeout-ms))
