; Client of a CeTTa worker endpoint: one CWP1 request and its reply over a
; local Unix seqpacket socket, bounded by a timeout in milliseconds. For an
; agent's own loop talking to its durable channel service.
;
; A reply is (cwp:reply CODE NAME BODY); a service that cannot be reached in
; time is (cwp:unavailable REASON). Request codes: 1 next, 2 result, 4 submit.
; Reply codes: 64 idle, 65 task, 66 stored, 67 pending, 68 unknown,
; 69 conflict, 70 invalid, 71 unavailable, 72 limit.

(= (cwp:call $path $code $name $body $timeout)
   (function
     (case (__cetta_lib_cwp_call $path $code $name $body $timeout)
       (($result (return $result))))))

(= (cwp:next $path $after) (cwp:call $path 1 $after "" 5000))
(= (cwp:result $path $task $body) (cwp:call $path 2 $task $body 5000))
(= (cwp:submit $path $key $body) (cwp:call $path 4 $key $body 5000))
