; Run a program as a child process. No shell is involved: ARGV is the
; program and its arguments. The child starts in CWD with ENV as its whole
; environment, since nothing of this process's environment is inherited, and
; with only standard input (empty), output and error open. It runs in a
; process group of its own, and the whole group is killed when TIMEOUT-MS
; runs out. Each of its output and its error is kept up to MAX-BYTES bytes
; and read to the end past that.
;
;   (proc:run ("program" "argument" ...) "cwd" (("NAME" "value") ...)
;             timeout-ms max-bytes)
;
; A program named without a / is looked up on the PATH of ENV. The answer is
;   (proc:exited CODE "output" "error")
;   (proc:signaled SIGNAL "output" "error")
;   (proc:timed-out "output" "error")
;   (proc:failed "reason")          the program could not be started
; Bytes that are not UTF-8 read as U+FFFD. Under speculative evaluation the
; operation is refused.

(= (proc:run $argv $cwd $env $timeout $max)
   (function
     (case (__cetta_lib_proc_run $argv $cwd $env $timeout $max)
       (($result (return $result))))))
