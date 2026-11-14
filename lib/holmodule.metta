; The HOL Light kernel stated as a rule module (fusion.ml and the HOLR replay as rules), compiled to
; procedures by rulec.  Import with (import! &self holmodule).
; (holm:check PATH POLICY): the module's verdict for a HOLR stream, as module data:
;   (hl:accepted RECORDS THEOREMS) | (hl:refused STATUS REASON RECORDS) | (hl:malformed REASON RECORDS)
;   | (hl:probe-refused FAILED), numerals as binary numerals, names as byte lists.
; (holm:measure PATH POLICY): (holm:measure RECORDS DECODE-NS MODULE-NS YOUNG-BYTES PROMOTED-BYTES
;   OLD-PEAK-BYTES) for the same run.
; POLICY is open, standard or sealed.  A call that cannot run gives (holm:error "message").

(: holm:check (-> %Undefined% %Undefined% Atom))
(: __cetta_lib_holm_check (-> Atom Atom Atom))
(= (holm:check $path $policy)
   (function (chain (eval (__cetta_lib_holm_check $path $policy)) $result (return $result))))

(: holm:measure (-> %Undefined% %Undefined% Atom))
(: __cetta_lib_holm_measure (-> Atom Atom Atom))
(= (holm:measure $path $policy)
   (function (chain (eval (__cetta_lib_holm_measure $path $policy)) $result (return $result))))
