!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self system)

!(let $records (tptp:read-file (system:arg 0))
   (let $reparsed (tptp:read-text (tptp:print $records))
     (println!
       (TptpCanonicalPrintRealV1
         (== $records $reparsed)
         (tptp:record-statistics $reparsed)))))
