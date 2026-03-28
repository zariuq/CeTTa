(= (system-args)
   (function
     (case (__cetta_lib_system_args ())
       (($result (return $result))))))

(= (system-arg $index)
   (function
     (case (__cetta_lib_system_arg $index)
       (($result (return $result))))))

(= (system-argc)
   (function
     (case (__cetta_lib_system_arg_count ())
       (($result (return $result))))))

(= (system-has-args)
   (function
     (case (__cetta_lib_system_has_args ())
       (($result (return $result))))))

(= (system-getenv-or-default $name $default)
   (function
     (case (__cetta_lib_system_getenv_or_default $name $default)
       (($result (return $result))))))

(= (system-flag-arg? $arg)
   (function
     (case (__cetta_lib_system_is_flag_arg $arg)
       (($result (return $result))))))

(= (system-exit $code)
   (function
     (case (__cetta_lib_system_exit_with_code $code)
       (($result (return $result))))))

(= (system-cwd)
   (function
     (case (__cetta_lib_system_cwd ())
       (($result (return $result))))))
