; Process observations preserve bytes and distinguish exit, signal, launch
; failure and interruption. Options are explicit (name value) pairs. A zero
; timeout or capture limit means no bound. Text decoding is a separate policy.
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
