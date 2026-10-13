; Portable, nonblocking host-I/O boundary.
;
; A provider may finish work between evaluator turns, but completion enters
; MeTTa only through io:poll or the native blocking io:wait. Requests receive
; host-assigned correlation IDs; each completion is observed at most once.

(: io:capabilities (-> Expression))
(: io:submit (-> Atom Expression))
(: io:poll (-> Expression))
(: io:poll (-> Number Expression))
(: io:status (-> Number Expression))
(: io:wait (-> Number Expression))
(: io:wait (-> Number Number Expression))
(: io:wait (-> Number String Expression))
(: io:wait (-> Number Number String Expression))
(: io:cancel (-> Number Expression))
(: io:signal (-> Number Number Expression))

(: http:request (-> Atom Atom Expression String Number Number Atom))
(: http:request (-> Atom Atom Expression String Number Number Bool Atom))
(: http:request (-> Atom Atom Expression String Number Number Bool Atom Atom))
(: http:timeouts (-> Number Number Number Atom))
(: http:header (-> String String Atom))
(: http:response (-> Number String Atom))
; Add retry-after after the three http:timeouts values to request metadata.
; The response carries Retry-After seconds, or -1 when the header is absent.
; Requests without that option retain the ordinary http:response shape.
(: http:response-with-metadata (-> Number String Atom Atom))
(: http:retry-after (-> Number Atom))

(= (io:capabilities)
   (__cetta_lib_io_capabilities ()))

(= (io:submit $request)
   (__cetta_lib_io_submit $request))

(= (io:poll)
   (__cetta_lib_io_poll ()))

; Owned polling leaves every other request's completion queued. A consumed or
; cancelled ID answers io:unknown; an existing unfinished request answers idle.
(= (io:poll $id)
   (__cetta_lib_io_poll $id))

; Non-consuming metadata. Byte activity is not a completed provider answer.
(= (io:status $id)
   (__cetta_lib_io_status $id))

(= (io:cancel $id)
   (__cetta_lib_io_cancel $id))

; A signal leaves the completion observable. Cancel instead consumes the
; handle and stops its process group. Timers and processes work without HTTP.
(= (io:signal $id $signal)
   (__cetta_lib_io_signal $id $signal))

; Native provider data, not a program to evaluate on the worker thread:
; (timer:after milliseconds)
; (proc:request argv cwd env timeout-ms stdout-file stderr-file)
; Process timeout 0 means no deadline. Output files remain after completion.
; Status is (proc:running pid elapsed-ms stdout-file stderr-file), then
; (proc:finished pid exited|signaled|timed-out|failed code stdout-file stderr-file).
; Timer completion is (timer:fired elapsed-ms).

(= (http:get $url)
   (io:submit (http:request "GET" $url () "" 30000 8388608)))

(= (http:get $url $headers)
   (io:submit (http:request "GET" $url $headers "" 30000 8388608)))

(= (http:post $url $headers $body)
   (io:submit (http:request "POST" $url $headers $body 60000 8388608)))

(= (io:wait $timeout-ms)
   (__cetta_lib_io_wait $timeout-ms))

(= (io:wait $id $timeout-ms)
   (__cetta_lib_io_wait $id $timeout-ms))

; Optional progress observation uses the existing native process observer;
; it never changes execution or admits/refuses a command. The two-argument
; spelling also accepts timeout-ms and a text progress path (all providers).
(= (io:wait $id $timeout-ms $progress)
   (__cetta_lib_io_wait $id $timeout-ms $progress))

; Explicit error boundaries for clients that retain a pending operation.
; The generated dialect spellings preserve these boundaries; an application
; must not implement HE's function/return using PeTTa-only catch syntax.
(: io:try-submit (-> Atom %Undefined%))
(= (io:try-submit $request)
   (function (case (io:submit $request)
     (((io:pending $id) (return (io:pending $id)))
      ((Error $source $reason) (return (io:failed)))
      ($other (return (io:failed)))))))

(: io:try-status (-> Number %Undefined%))
(= (io:try-status $id)
   (function (case (io:status $id)
     (((io:progress $handle $phase $age $bytes $quiet)
       (return (io:progress $handle $phase $age $bytes $quiet)))
      ((Error $source $reason) (return (io:no-progress)))
      ($other (return (io:no-progress)))))))

(: io:try-wait (-> Number Number %Undefined%))
(= (io:try-wait $id $milliseconds)
   (function (case (io:wait $id $milliseconds)
     (((io:idle) (return (io:idle)))
      ((io:event $received $outcome) (return (io:event $received $outcome)))
      ((Error $source $reason) (return (io:failed)))
      ($other (return (io:failed)))))))
