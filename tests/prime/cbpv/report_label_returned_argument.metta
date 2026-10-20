; A demand report counts a parameter the body returns as a use.  k returns
; its first argument, so the answer boundary demands that argument once on
; every path: eager and lazy give the same answers there
; (TypeTheory/ContextualCBPVLicense.licensed_iff_map with n = 1, and
; GSLT.Dynamics.DemandAgreement.eagerShared_eq_lazyShared_iff).  The report
; should apply eager at that site, under a committed bubble and under a
; licensed one.  A parameter the body never uses stays kept.  The rows state
; that reading; the draft currently labels the returned argument
; may-discard-answers.  The same decisions, computed by the elaboration, are
; in elaborate_bubbles.metta.
(= coin heads)
(= coin tails)
(= (k $x $y) $x)
(= (k2 $x $y) $y)
!(demand:report (demand:committed eager (k (k 1 coin) coin)))
!(demand:report (demand:licensed eager (k (k 1 coin) coin)))
; A parameter the body never uses: kept.
!(demand:report (demand:licensed eager (k2 coin 1)))
