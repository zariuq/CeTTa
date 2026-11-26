(: system:monotonic-ns (-> Number))
(: system:cwd (-> String))
(: system:runtime-root (-> String))

(= (system:args)
   (function
     (case (__cetta_lib_system_args ())
       (($result (return $result))))))

(= (system:arg $index)
   (function
     (case (__cetta_lib_system_arg $index)
       (($result (return $result))))))

(= (system:argc)
   (function
     (case (__cetta_lib_system_arg_count ())
       (($result (return $result))))))

(= (system:has-args)
   (function
     (case (__cetta_lib_system_has_args ())
       (($result (return $result))))))

(= (system:getenv-or-default $name $default)
   (function
     (case (__cetta_lib_system_getenv_or_default $name $default)
       (($result (return $result))))))

(= (system:is-flag $arg)
   (function
     (case (__cetta_lib_system_is_flag_arg $arg)
       (($result (return $result))))))

(= (system:exit $code)
   (function
     (case (__cetta_lib_system_exit_with_code $code)
       (($result (return $result))))))

(= (system:write $stream $bytes)
   (function (case (__cetta_lib_system_write $stream $bytes)
       (($result (return $result))))))

; (system:read-line): the next line of the standard input with its end of
; line; "" at the end of the input
(= (system:read-line)
   (function (case (__cetta_lib_system_read_line)
       (($result (return $result))))))

(= (system:cwd)
   (function
     (case (__cetta_lib_system_cwd ())
       (($result (return $result))))))

; the process: its environment as (NAME VALUE) pairs, its id, the processors
; online (() when unknown) and uname's (SYSNAME NODENAME RELEASE VERSION MACHINE)
(= (system:environ)
   (function
     (case (__cetta_lib_system_environ ())
       (($result (return $result))))))

(= (system:getpid)
   (function
     (case (__cetta_lib_system_getpid ())
       (($result (return $result))))))

(= (system:cpu-count)
   (function
     (case (__cetta_lib_system_cpu_count ())
       (($result (return $result))))))

(= (system:uname)
   (function
     (case (__cetta_lib_system_uname ())
       (($result (return $result))))))

; The canonical directory of the top-level entry file, independent of imports.
(= (system:entry-directory)
   (function (case (__cetta_lib_system_entry_directory ())
     (($result (return $result))))))

; The runtime's library and language-data root, independent of the entry file.
(= (system:runtime-root)
   (function (case (__cetta_lib_system_runtime_root ())
     (($result (return $result))))))

(= (system:monotonic-ns)
   (function
     (case (__cetta_lib_system_monotonic_ns ())
       (($result (return $result))))))

; Time one explicitly delayed Prime computation.  Each `case` is a demand
; boundary: the start clock is read before the suspension is forced, and the
; end clock is read only after its selected answer is available.
(= (system:timed $suspension)
   (case (system:monotonic-ns)
     (($start
       (case (force $suspension)
         (($value
           (case (system:monotonic-ns)
             (($end
               (system:timed-result (- $end $start) $value)))))))))))
