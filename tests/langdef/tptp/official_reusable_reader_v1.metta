!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self system)

(= (reuse:first-record $records) (car-atom $records))

(= (reuse:summary $reader)
   (let $text-record
        (reuse:first-record
          (tptp:read-text $reader "fof(reused,axiom,p(a))."))
     (let $file-record
          (reuse:first-record
            (tptp:read-file $reader (system:arg 1)))
       (TptpReusableReaderV1
         (tptp:reader-info $reader)
         (tptp:record-family $text-record)
         (tptp:record-name $text-record)
         (tptp:record-family $file-record)
         (tptp:record-name $file-record)))))

!(case (tptp:official-reader (system:arg 0))
   (((NativeHandle "cetta.tptp-reader.v1" $_id)
      (println! (reuse:summary (NativeHandle "cetta.tptp-reader.v1" $_id))))
    ($fault (println! (TptpReusableReaderFailureV1 $fault)))))
