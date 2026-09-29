!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self system)

(= (print-test:roundtrip $records)
   (let $printed (tptp:print $records)
     (let $reparsed (tptp:read-text $printed)
       (TptpCanonicalPrintRoundTripV1
         (== $records $reparsed)
         (tptp:record-statistics $reparsed)))))
(= (print-test:family $family $path)
   (let $records (tptp:read-file $path)
     (print-test:roundtrip (tptp:select-family $family $records))))

!(println! (print-test:family fof (system:arg 0)))
!(println! (print-test:family cnf (system:arg 0)))
!(println! (print-test:family tff (system:arg 0)))
!(println! (print-test:family thf (system:arg 0)))
!(println! (print-test:family tcf (system:arg 0)))
!(println! (print-test:family tpi (system:arg 0)))
!(println! (print-test:family include (system:arg 0)))
!(println! (TptpCanonicalPrintPathRoundTripV1 1
             (print-test:roundtrip (tptp:read-file (system:arg 1)))))
!(println! (TptpCanonicalPrintPathRoundTripV1 2
             (print-test:roundtrip (tptp:read-file (system:arg 2)))))
!(println! (TptpCanonicalPrintPathRoundTripV1 3
             (print-test:roundtrip (tptp:read-file (system:arg 3)))))
!(println! (TptpCanonicalPrintPathRoundTripV1 4
             (print-test:roundtrip (tptp:read-file (system:arg 4)))))
!(println!
   (TptpCanonicalPrintLexicalKindsV1
     (print-test:roundtrip
       (tptp:read-text
         "fof(42,axiom,p(42,1/3,-2.5,\"distinct value\"))."))))
!(println!
   (tptp:print
     (tptp:read-text
       "fof(a,axiom,p).\nfof(a,conjecture,~p).\n")))
