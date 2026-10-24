; Portable, nonblocking host-I/O boundary.
;
; A provider may finish work between evaluator turns, but completion enters
; MeTTa only through io:poll or the native blocking io:wait. Requests receive
; host-assigned correlation IDs; each completion is observed at most once.

(: io:capabilities (-> Expression))
(: io:submit (-> %Undefined% Expression))
(: io:poll (-> Expression))
(: io:poll (-> Number Expression))
(: io:status (-> Number Expression))
(: io:wait (-> Number Expression))
; The two-argument spelling is id/timeout or timeout/progress-path. One
; declaration describes both shapes; the native call validates the values.
(: io:wait (-> Number %Undefined% Expression))
(: io:wait (-> Number Number String Expression))
(: io:wait-ready (-> Number Expression))
(: io:wait-ready (-> %Undefined% Number Expression))
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

; Native outcomes cross as completed data. The shared value-handoff spelling
; prevents type alternatives from re-executing an effect in PeTTa.
(: __cetta_lib_io_capabilities (-> Atom Atom))
(: __cetta_lib_io_submit (-> Atom Atom))
(: __cetta_lib_io_poll (-> Atom Atom))
(: __cetta_lib_io_status (-> Atom Atom))
(: __cetta_lib_io_wait (-> Atom Atom))
(: __cetta_lib_io_wait (-> Atom Atom Atom))
(: __cetta_lib_io_wait (-> Atom Atom Atom Atom))
(: __cetta_lib_io_wait_ready (-> Atom Atom))
(: __cetta_lib_io_wait_ready (-> Atom Atom Atom))
(: __cetta_lib_io_cancel (-> Atom Atom))
(: __cetta_lib_io_signal (-> Atom Atom Atom))

(= (io:capabilities)
   (function (chain (eval (__cetta_lib_io_capabilities ())) $result (return $result))))

(= (io:submit $request)
   (function (chain (eval (__cetta_lib_io_submit $request)) $result (return $result))))

(= (io:poll)
   (function (chain (eval (__cetta_lib_io_poll ())) $result (return $result))))

; Owned polling leaves every other request's completion queued. A consumed or
; cancelled ID answers io:unknown; an existing unfinished request answers idle.
(= (io:poll $id)
   (function (chain (eval (__cetta_lib_io_poll $id)) $result (return $result))))

; Non-consuming metadata. Byte activity is not a completed provider answer.
(= (io:status $id)
   (function (chain (eval (__cetta_lib_io_status $id)) $result (return $result))))

(= (io:cancel $id)
   (function (chain (eval (__cetta_lib_io_cancel $id)) $result (return $result))))

; A signal leaves the completion observable. Cancel instead consumes the
; handle and stops its process group. Timers and processes work without HTTP.
(= (io:signal $id $signal)
   (function (chain (eval (__cetta_lib_io_signal $id $signal)) $result (return $result))))

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
   (function (chain (eval (__cetta_lib_io_wait $timeout-ms)) $result (return $result))))

(= (io:wait $id $timeout-ms)
   (function (chain (eval (__cetta_lib_io_wait $id $timeout-ms)) $result (return $result))))

; Readiness leaves every completion available to its owner. It returns
; (io:ready (id ...)) or (io:idle); the consuming io:wait is unchanged.
(= (io:wait-ready $timeout-ms)
   (function (chain (eval (__cetta_lib_io_wait_ready $timeout-ms)) $result (return $result))))
(= (io:wait-ready $id $timeout-ms)
   (function (chain (eval (__cetta_lib_io_wait_ready $id $timeout-ms)) $result (return $result))))

; Optional progress observation uses the existing native process observer;
; it never changes execution or admits/refuses a command. The two-argument
; spelling also accepts timeout-ms and a text progress path (all providers).
(= (io:wait $id $timeout-ms $progress)
   (function (chain (eval (__cetta_lib_io_wait $id $timeout-ms $progress)) $result (return $result))))

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
      ((proc:running $pid $age $out $err)
       (return (proc:running $pid $age $out $err)))
      ((proc:finished $pid $state $code $out $err)
       (return (proc:finished $pid $state $code $out $err)))
      ((timer:status $phase $age) (return (timer:status $phase $age)))
      ((Error $source $reason) (return (io:no-progress)))
      ($other (return (io:no-progress)))))))

(: io:try-wait (-> Number Number %Undefined%))
(= (io:try-wait $id $milliseconds)
   (function (case (io:wait $id $milliseconds)
     (((io:idle) (return (io:idle)))
      ((io:event $received $outcome) (return (io:event $received $outcome)))
      ((Error $source $reason) (return (io:failed)))
      ($other (return (io:failed)))))))
