; Portable, nonblocking host-I/O boundary.
;
; A provider may finish work between evaluator turns, but completion enters
; MeTTa only through io:poll.  Requests receive host-assigned correlation IDs,
; and each completion can be observed at most once.

(: io:capabilities (-> Expression))
(: io:submit (-> Atom Atom))
(: io:poll (-> Atom))
(: io:cancel (-> Number Atom))

(: http:request (-> Atom Atom Expression String Number Number Atom))
(: http:header (-> String String Atom))
(: http:response (-> Number String Atom))

(= (io:capabilities)
   (function
     (case (__cetta_lib_io_capabilities ())
       (($result (return $result))))))

(= (io:submit $request)
   (function
     (case (__cetta_lib_io_submit $request)
       (($result (return $result))))))

(= (io:poll)
   (function
     (case (__cetta_lib_io_poll ())
       (($result (return $result))))))

(= (io:cancel $id)
   (function
     (case (__cetta_lib_io_cancel $id)
       (($result (return $result))))))

(= (http:get $url)
   (io:submit (http:request "GET" $url () "" 30000 8388608)))

(= (http:get $url $headers)
   (io:submit (http:request "GET" $url $headers "" 30000 8388608)))

(= (http:post $url $headers $body)
   (io:submit (http:request "POST" $url $headers $body 60000 8388608)))
