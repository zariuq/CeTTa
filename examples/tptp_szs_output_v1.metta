!(import! &self "../lib/petta/list.metta")
!(import! &self "../lib/petta/lib_tptp.metta")

; Solver-reported status, not kernel-checked theoremhood.
!(test (tptp:szs-status (tptp:szs-output "% SZS status Theorem for example\n"))
       (TPTP:SZSReported "Theorem"))
; Complete payloads keep their own data form and source-line range.
!(test
   (tptp:szs-data
     (tptp:szs-output "% SZS output start Proof for example\nfof(a,axiom,p).\n% SZS output end Proof for example"))
   ((TPTP:SZSData "Proof" "fof(a,axiom,p)." 0 2)))
; Negative: an opening marker and some data are not completion evidence.
!(test
   (tptp:szs-pending (tptp:szs-output "% SZS output start Proof for example\nfof(a,axiom,p)."))
   (TPTP:SZSUnfinished "Proof" "fof(a,axiom,p)." 0))
