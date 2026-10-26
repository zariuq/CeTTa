; Process observations preserve bytes and distinguish exit, signal, launch
; failure and interruption. Options are explicit (name value) pairs. A zero
; timeout or capture limit means no bound in capture mode (the default).
; Text decoding is a separate policy.
; lifetime owner selects tree ownership through the Linux process companion;
; the default lifetime group retains process-group cancellation.
(= (proc:spawn $argv $options)
   (__cetta_lib_proc_spawn $argv $options))
(= (proc:poll $owner)
   (__cetta_lib_proc_poll $owner))
(= (proc:wait $owner $milliseconds)
   (__cetta_lib_proc_wait $owner $milliseconds))
(= (proc:status $owner)
   (__cetta_lib_proc_status $owner))
(= (proc:cancel $owner)
   (__cetta_lib_proc_cancel $owner))
(= (proc:close $owner)
   (__cetta_lib_proc_close $owner))
(= (proc:abort-signal)
   (__cetta_lib_proc_abort_signal ()))

; proc:wait is bounded; pending neither consumes nor completes a process.
; Cooperative await and synchronous run retain the same owned job.
(= (proc:await $owner)
   (__cetta_lib_proc_await $owner))
(= (proc:run $argv $options)
   (__cetta_lib_proc_run $argv $options))

; (mode stream) requires a positive max-bytes. It bounds each pending pipe
; queue, not lifetime output: full output queues apply backpressure, not loss.
; stdin stays open until close-input; already queued bytes are sent first.
; write atomically queues the whole string or returns proc:backpressure.
; (proc:queued n) acknowledges ownership, not delivery to the child.
(= (proc:write $owner $bytes)
   (__cetta_lib_proc_write $owner $bytes))
(= (proc:close-input $owner)
   (__cetta_lib_proc_close_input $owner))
; read consumes available bytes as (proc:chunk bytes). A zero wait polls.
; proc:pending is not EOF; proc:eof means the pipe actually reached EOF.
; Callers frame messages and drain both stdout and stderr before awaiting
; completion. Cancellation/close remain available under backpressure.
; Final proc:stream-capture records pending/observed/delivered byte counts
; for stdout then stderr, their EOF states, and (proc:input sent queued).
(= (proc:read $owner $stream $milliseconds)
   (__cetta_lib_proc_read $owner $stream $milliseconds))
