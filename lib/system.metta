(: system:monotonic-ns (-> Number))
(: system:cwd (-> String))

(= (system:wall-time)
   (function (case (__cetta_lib_system_host wall-time)
     (($result (return $result))))))
(= (system:pid)
   (function (case (__cetta_lib_system_host pid)
     (($result (return $result))))))
(= (system:pid-alive? $pid)
   (function (case (__cetta_lib_system_host pid-alive $pid)
     (($result (return $result))))))
(= (system:format-time $format $timestamp $zone)
   (function (case (__cetta_lib_system_host format-time $format $timestamp $zone)
     (($result (return $result))))))
(= (system:read-line)
   (function (case (__cetta_lib_system_read_line ())
     (($result (return $result))))))

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

(= (system:cwd)
   (function
     (case (__cetta_lib_system_cwd ())
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
