(: system:monotonic-ns (-> Number))
(: system:cwd (-> String))
(: system:runtime-root (-> String))

; time.time(): the wall clock's nanoseconds converted as CPython converts them.
(= (system:wall-time)
   (function (case (__cetta_lib_system_host wall-time)
     (($result (return $result))))))
(= (system:pid)
   (function (case (__cetta_lib_system_host pid)
     (($result (return $result))))))
; uuid.uuid4().hex: sixteen bytes from the kernel's random source, with the
; version 4 and RFC 4122 variant bits set, as 32 lowercase hexadecimal digits.
(= (system:uuid4-hex)
   (function (case (__cetta_lib_system_host uuid4-hex)
     (($result (return $result))))))
(= (system:pid-alive? $pid)
   (function (case (__cetta_lib_system_host pid-alive $pid)
     (($result (return $result))))))
; os.kill(pid, 0): True, or (system:os-error errno message) for every refusal,
; EPERM included. 0 and negative IDs address process groups, as kill(2) has it.
(= (system:signal-probe $pid)
   (function (case (__cetta_lib_system_host signal-probe $pid)
     (($result (return $result))))))
; getattr(errno, name) for the names Python's errno module knows: the
; platform's value, or () where the platform has no such errno.
(= (system:errno-value $name)
   (function (case (__cetta_lib_system_host errno-value $name)
     (($result (return $result))))))
; os.path.expanduser: "~" alone or before "/" becomes $HOME when it is set
; (even empty), else the account's home from the account database; "~name"
; becomes that account's home. An account that cannot be found leaves the path
; unchanged. Trailing slashes of the home are dropped; an empty result is "/".
(= (system:expanduser $path)
   (function (case (__cetta_lib_system_host expanduser $path)
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

(= (system:write $stream $bytes)
   (function (case (__cetta_lib_system_write $stream $bytes)
       (($result (return $result))))))

(= (system:cwd)
   (function
     (case (__cetta_lib_system_cwd ())
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
