; Native-heavy CeTTa compatibility layer for the most commonly used lib_zar
; surfaces. Keep the core Petta runtime native; use the generic Prolog seam
; only for true library-tier predicate imports.

!(import! &self lib_import)
!(import! &self ../system)

(: consult_file (-> Atom (->)))
(= (consult_file $prologfile)
   (prolog-consult! $prologfile))

(: use_module_file (-> Atom (->)))
(= (use_module_file $prologfile)
   (prolog-use-module! $prologfile))

(: get_cmdline_args (-> Expression))
(= (get_cmdline_args)
   (function
     (case (system-args)
       (($result (return $result))))))

(: get_cmdline_arg (-> Number Atom))
(= (get_cmdline_arg $idx)
   (function
     (case (system-arg $idx)
       (($result (return $result))))))

(: get_cmdline (-> Number Atom))
(= (get_cmdline $idx)
   (get_cmdline_arg $idx))

(: cmdline_arg_count (-> Number))
(= (cmdline_arg_count)
   (function
     (case (system-argc)
       (($result (return $result))))))

(: has_cmdline_args (-> Atom))
(= (has_cmdline_args)
   (function
     (case (system-has-args)
       ((True (return true))
        (False (return false))))))

(: get_env_var_or_default (-> Atom Atom Atom))
(= (get_env_var_or_default $name $default)
   (function
     (case (system-getenv-or-default $name $default)
       (($result (return $result))))))

(: halt_with_code (-> Number (->)))
(= (halt_with_code $code)
   (function
     (case (system-exit $code)
       (($result (return $result))))))

(: is_flag_arg (-> Atom Atom))
(= (is_flag_arg $arg)
   (case (system-flag-arg? $arg)
     ((True true)
      (False false))))

(: starts-with-dash (-> Atom Atom))
(= (starts-with-dash $arg)
   (case (system-flag-arg? $arg)
     ((True true)
      (False false))))

(: get-first-non-flag-arg (-> Number Atom))
(= (get-first-non-flag-arg $idx)
   (if (>= $idx (cmdline_arg_count))
       Empty
       (let $arg (get_cmdline_arg $idx)
         (if (== $arg Empty)
             Empty
             (case (system-flag-arg? $arg)
               ((True (get-first-non-flag-arg (+ $idx 1)))
                (False $arg)))))))

(: adjust-state-additive (-> Atom Number (->)))
(= (adjust-state-additive $state-space $delta)
   (let $current (get-state $state-space)
     (let $_ (change-state! $state-space (+ $current $delta))
       ())))
