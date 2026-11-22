; The HOL Light kernel stated as a rule module (fusion.ml and the HOLR replay as rules), compiled to
; procedures by rulec.  Import with (import! &self holmodule).
; (holm:check PATH POLICY): the module's verdict for a HOLR stream, as module data:
;   (hl:accepted RECORDS THEOREMS) | (hl:refused STATUS REASON RECORDS) | (hl:malformed REASON RECORDS)
;   | (hl:probe-refused FAILED), numerals as binary numerals, names as byte lists.  The stream is read
;   by the HOLR reader compiled from its GSLT modules while the kernel replays it, so a stream the
;   kernel refuses is not read further.
; (holm:measure PATH POLICY): (holm:measure RECORDS READ-NS MODULE-NS YOUNG-BYTES PROMOTED-BYTES
;   OLD-PEAK-BYTES) for the same run: the records read, the time in the reader, the rest of the run.
; POLICY is open, standard or sealed.  A call that cannot run gives (holm:error "message").
; The module's judgments are also called directly, as for every compiled module: (holm:call TERM),
; (holm:hold TERM), (holm:call-hold TERM), (holm:view H), (holm:release H), (holm:reset),
; (holm:stats), with TERM = (JUDGMENT ARG ...) in the module's constructors; and on a stream,
; (holm:feed PATH TERM), TERM a call one of whose arguments is (holm:stream).  Results (holm:value V),
; (holm:none) or (holm:error "message"); handles (holm:handle N) as for mgmodule.

(: holm:host (-> Atom Atom))
(= (holm:host open) (hl:open))
(= (holm:host standard) (hl:standard))
(= (holm:host sealed) (hl:sealed))
(= (holm:host "open") (hl:open))
(= (holm:host "standard") (hl:standard))
(= (holm:host "sealed") (hl:sealed))

(: holm:check (-> %Undefined% %Undefined% Atom))
(: __cetta_lib_holm_feed (-> Atom Atom Atom))
(= (holm:check $path $policy)
   (function
     (case (holm:host $policy)
       (((holm:host $other) (return (holm:error "axiom policy is open, standard or sealed")))
        ($host
          (case (__cetta_lib_holm_feed $path (hl:check $host (holm:stream)))
            (((holm:value $verdict) (return $verdict))
             ((holm:none) (return (holm:error "the module gives no verdict (a judgment failed)")))
             ($refusal (return $refusal)))))))))

(: holm:measure (-> %Undefined% %Undefined% Atom))
(: __cetta_lib_holm_feed-measure (-> Atom Atom Atom))
(= (holm:measure $path $policy)
   (function
     (case (holm:host $policy)
       (((holm:host $other) (return (holm:error "axiom policy is open, standard or sealed")))
        ($host
          (case (__cetta_lib_holm_feed-measure $path (hl:check $host (holm:stream)))
            (($result (return $result)))))))))

(: holm:feed (-> %Undefined% Atom Atom))
(= (holm:feed $path $term)
   (function (chain (eval (__cetta_lib_holm_feed $path $term)) $result (return $result))))

(: holm:call (-> Atom Atom))
(: __cetta_lib_holm_call (-> Atom Atom))
(= (holm:call $term)
   (function (chain (eval (__cetta_lib_holm_call $term)) $result (return $result))))

(: holm:hold (-> Atom Atom))
(: __cetta_lib_holm_hold (-> Atom Atom))
(= (holm:hold $term)
   (function (chain (eval (__cetta_lib_holm_hold $term)) $result (return $result))))

(: holm:call-hold (-> Atom Atom))
(: __cetta_lib_holm_call-hold (-> Atom Atom))
(= (holm:call-hold $term)
   (function (chain (eval (__cetta_lib_holm_call-hold $term)) $result (return $result))))

(: holm:view (-> Atom Atom))
(: __cetta_lib_holm_view (-> Atom Atom))
(= (holm:view $handle)
   (function (chain (eval (__cetta_lib_holm_view $handle)) $result (return $result))))

(: holm:release (-> Atom Atom))
(: __cetta_lib_holm_release (-> Atom Atom))
(= (holm:release $handle)
   (function (chain (eval (__cetta_lib_holm_release $handle)) $result (return $result))))

(: holm:reset (-> Atom))
(: __cetta_lib_holm_reset (-> Atom))
(= (holm:reset)
   (function (chain (eval (__cetta_lib_holm_reset)) $result (return $result))))

(: holm:stats (-> Atom))
(: __cetta_lib_holm_stats (-> Atom))
(= (holm:stats)
   (function (chain (eval (__cetta_lib_holm_stats)) $result (return $result))))
