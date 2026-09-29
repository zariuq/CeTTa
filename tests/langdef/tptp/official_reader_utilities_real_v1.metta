!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self system)

(= (utilities-real-v1:run $path)
   (let $file (tptp:read-file $path)
     (TptpOfficialReaderUtilitiesRealV1
       (tptp:record-count $file)
       (tptp:record-statistics $file)
       (tptp:record-count (tptp:select-family fof $file))
       (tptp:record-count (tptp:select-role axiom $file)))))

!(println! (utilities-real-v1:run (system:arg 0)))
