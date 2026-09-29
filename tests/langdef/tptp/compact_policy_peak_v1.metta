; One TPTP file read under one compact policy.  Arguments: the file, then the
; policy.  The record count shows that the read completed; the gate measures
; the peak of the process around it.
!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self system)

!(case (tptp:read-file-document (tptp:open-reader strict) (system:arg 1) (system:arg 0))
   (((TPTP:SourceDocument $identity $digest $text $records $spans)
      (println! (CompactPolicyPeakV1 (system:arg 1) (let $all $records (size-atom $all)))))
    ($other (println! (CompactPolicyPeakV1Fault $other)))))
