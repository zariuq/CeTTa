!(import! &self ../../../lib/lib_tptp.metta)

(= (contract-v1:only-variable [(fof $_name $_role ($_head $variable))])
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
