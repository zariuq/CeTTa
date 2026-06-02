; ============================================================================
; STAMP OVERLAP SWEEP --- provenance pattern study
; ============================================================================
;
; Generalization of stamp_collision_ablation.metta from one overlap pattern
; to five.  Same Roman 4-node network throughout; only the stamp assignments
; differ.
;
; The point: "do stamps matter?" is too coarse a question.  The right
; question is "WHICH overlap classes do they actually control?"
;
; Five patterns:
;
;   P0  fully-disjoint     (1)(2)(3)(4)
;       Each fact has its own stamp.  Two A->D derivations have stamps
;       (1 2) and (3 4), disjoint.  Revision fires.
;       Expected top: (stv 0.5 0.9473684...) with stamps (1 2 3 4).
;
;   P1  share-on-A         (1)(2)(1)(4)
;       A->B and A->C both extracted from the same observation.
;       A->D via B has (1 2); A->D via C has (1 4); overlap on {1}.
;       Revision blocked.  Expected top: (stv 0.5 0.9) with stamps (1 2) or (1 4).
;
;   P2  share-on-D         (1)(2)(3)(2)
;       B->D and C->D both extracted from the same observation.
;       A->D via B has (1 2); A->D via C has (2 3); overlap on {2}.
;       Revision blocked.  Expected top: (stv 0.5 0.9) with stamps (1 2) or (2 3).
;
;   P3  identical-paths    (1)(2)(1)(2)
;       Both A->{B,C} share stamp 1; both {B,C}->D share stamp 2.
;       A->D via B has (1 2); A->D via C has (1 2); identical.
;       Revision blocked.  Expected top: (stv 0.5 0.9) with stamps (1 2).
;
;   P4  disjoint-but-noisy (1 5)(2)(3 6)(4)
;       Each premise carries extra stamps that are independent across paths.
;       A->D via B has (1 2 5); A->D via C has (3 4 6); disjoint.
;       Revision fires --- noise doesn't break it.
;       Expected top: (stv 0.5 0.9473684...) with stamps (1 2 3 4 5 6).
;
; What the table shows: the gate cares about set-intersection of stamps,
; not about which positions carry which atoms.  P0 and P4 BOTH revise
; (despite P4 having larger stamps) because both have disjoint trajectory
; provenance.  P1, P2, P3 all block --- and the blocked confidence is the
; SAME (0.9) regardless of which premise pair caused the overlap.
;
; Run:  ./cetta examples/stamp_overlap_sweep.metta
; ============================================================================

!(import! &self ../lib/lib_wmpln)

(= (STV A) (stv 0.5 0.9))
(= (STV B) (stv 0.25 0.9))
(= (STV C) (stv 0.25 0.9))
(= (STV D) (stv 0.5 0.9))

; ----- Pattern KBs -----

(= (kb-P0)
   ((Sentence ((Inheritance A B) (stv 0.25 0.9)) (1))
    (Sentence ((Inheritance B D) (stv 0.5  0.9)) (2))
    (Sentence ((Inheritance A C) (stv 0.25 0.9)) (3))
    (Sentence ((Inheritance C D) (stv 0.5  0.9)) (4))))

(= (kb-P1)
   ((Sentence ((Inheritance A B) (stv 0.25 0.9)) (1))
    (Sentence ((Inheritance B D) (stv 0.5  0.9)) (2))
    (Sentence ((Inheritance A C) (stv 0.25 0.9)) (1))   ; <- share with A->B
    (Sentence ((Inheritance C D) (stv 0.5  0.9)) (4))))

(= (kb-P2)
   ((Sentence ((Inheritance A B) (stv 0.25 0.9)) (1))
    (Sentence ((Inheritance B D) (stv 0.5  0.9)) (2))
    (Sentence ((Inheritance A C) (stv 0.25 0.9)) (3))
    (Sentence ((Inheritance C D) (stv 0.5  0.9)) (2))))  ; <- share with B->D

(= (kb-P3)
   ((Sentence ((Inheritance A B) (stv 0.25 0.9)) (1))
    (Sentence ((Inheritance B D) (stv 0.5  0.9)) (2))
    (Sentence ((Inheritance A C) (stv 0.25 0.9)) (1))   ; <- both shared
    (Sentence ((Inheritance C D) (stv 0.5  0.9)) (2))))

(= (kb-P4)
   ((Sentence ((Inheritance A B) (stv 0.25 0.9)) (1 5))
    (Sentence ((Inheritance B D) (stv 0.5  0.9)) (2))
    (Sentence ((Inheritance A C) (stv 0.25 0.9)) (3 6))
    (Sentence ((Inheritance C D) (stv 0.5  0.9)) (4))))

; Run each pattern through PLN.Query and report
!(println! "===========================================================")
!(println! " STAMP OVERLAP SWEEP --- Roman 4-node, varying stamps")
!(println! "===========================================================")
!(println! "")

!(println! "P0 fully-disjoint     (1)(2)(3)(4):")
!(println! ("    result:" (PLN.Query (kb-P0) (Inheritance A D) 25)))
!(println! "    expect: ((stv 0.5 0.9473684210526316) (1 2 3 4))")
!(println! "")

!(println! "P1 share-on-A         (1)(2)(1)(4):")
!(println! ("    result:" (PLN.Query (kb-P1) (Inheritance A D) 25)))
!(println! "    expect: ((stv 0.5 0.9) (1 2)) or ((stv 0.5 0.9) (1 4))")
!(println! "")

!(println! "P2 share-on-D         (1)(2)(3)(2):")
!(println! ("    result:" (PLN.Query (kb-P2) (Inheritance A D) 25)))
!(println! "    expect: ((stv 0.5 0.9) (1 2)) or ((stv 0.5 0.9) (2 3))")
!(println! "")

!(println! "P3 identical-paths    (1)(2)(1)(2):")
!(println! ("    result:" (PLN.Query (kb-P3) (Inheritance A D) 25)))
!(println! "    expect: ((stv 0.5 0.9) (1 2))")
!(println! "")

!(println! "P4 disjoint-but-noisy (1 5)(2)(3 6)(4):")
!(println! ("    result:" (PLN.Query (kb-P4) (Inheritance A D) 25)))
!(println! "    expect: ((stv 0.5 0.9473684210526316) (1 2 3 4 5 6))")
!(println! "")

!(println! "===========================================================")
!(println! " SWEEP COMPLETE")
!(println! "===========================================================")
!(println! "")
!(println! " The gate's behavior is determined by stamp-set intersection,")
!(println! " not by which premise pair carried the overlap.  P0 and P4 both")
!(println! " revise (disjoint sets); P1, P2, P3 all block (overlapping sets).")
!(println! " The blocked confidence is the SAME 0.9 regardless of which")
!(println! " specific overlap pattern caused it.")
