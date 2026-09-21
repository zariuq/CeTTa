!(import! &self ../../../lib/lib_tptp.metta)

(= (contract-v1:only-variable
     (tptp-rec:file
       (tptp-rec:inputs-cons
         (tptp-rec:fof $_name $_role
           (tptp-rec:atom $_head
             (tptp-rec:terms-cons
               (tptp-rec:var (tptp-rec:word-upper $variable))
               (tptp-rec:terms-nil)))
           $_annotation $_input-span)
         (tptp-rec:inputs-nil))
       $_file-span))
   $variable)

!(println!
   (TptpFrozenReaderContractV1
     (contract-v1:only-variable
       (tptp:read-text
         "fof(long,axiom,p(AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA))."))
     (tptp:read-text "fof(a,axiom,p). fof(b,axiom,).")
     (tptp:read-text "☃")
     (tptp:read-text (NativeHandle "cetta.tptp-reader.v1" -1) "fof(a,axiom,p).")
     (tptp:read-text (TPTP:DigestMismatch "wrong") "fof(a,axiom,p).")))
