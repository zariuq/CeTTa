; ============================================================================
; STAMP COLLISION ABLATION --- the StampDisjoint gate, load-bearing
; ============================================================================
;
; Companion to advection_trace_roman.metta. Same Roman 4-node network,
; but run TWICE with different stamp assignments:
;
;   KB-disjoint:  each of the four facts carries its own unique stamp.
;                 Two A->D derivations have stamps (1 2) and (3 4).
;                 StampDisjoint (1 2) (3 4) = True
;                 PLN.Derive's revision gate fires.
;                 Result: (stv 0.5 18/19) = (stv 0.5 0.9473...)
;
;   KB-collision: A->B and A->C SHARE a stamp element (both have stamp 1).
;                 The two A->D derivations end up with stamps (1 2) and (1 4).
;                 StampDisjoint (1 2) (1 4) = False (both contain 1).
;                 PLN.Derive's revision gate REFUSES to merge them.
;                 Result: (stv 0.5 0.9) --- confidence stays at one-path level.
;
; What this shows: the difference in confidence (0.9474 vs 0.9) is NOT a
; cosmetic re-labeling. It is the engine REFUSING to double-count when the
; two paths share a common ancestor. The StampDisjoint check at
; lib_wmpln.metta:446 is load-bearing. Remove it, and the second case
; would silently revise to the same 0.9474 as the first --- inflating
; confidence beyond what the evidence supports.
;
; This is the formal answer to "why bother with stamps at all": because
; without them, additive PLN revision is unsound on overlapping evidence.
;
; Run:  ./cetta examples/stamp_collision_ablation.metta
; Expect: TRACE lines for each case, then two PLN.Query outputs differing
;         in the confidence component and in the stamp tuple.
; ============================================================================

!(import! &self ../lib/lib_wmpln)

; Node priors (same as advection_trace_roman.metta)
(= (STV A) (stv 0.5 0.9))
(= (STV B) (stv 0.25 0.9))
(= (STV C) (stv 0.25 0.9))
(= (STV D) (stv 0.5 0.9))

; ----------------------------------------------------------------------------
; Case 1: KB-disjoint (independent provenance for both paths)
; ----------------------------------------------------------------------------
; Each fact has its own stamp; the two A->D derivations are stamp-disjoint;
; Truth_Revision fires, confidence climbs to 18/19.
(= (kb-disjoint)
   ((Sentence ((Inheritance A B) (stv 0.25 0.9)) (1))
    (Sentence ((Inheritance B D) (stv 0.5  0.9)) (2))
    (Sentence ((Inheritance A C) (stv 0.25 0.9)) (3))
    (Sentence ((Inheritance C D) (stv 0.5  0.9)) (4))))

; ----------------------------------------------------------------------------
; Case 2: KB-collision (A->B and A->C from a shared observation)
; ----------------------------------------------------------------------------
; A->B and A->C BOTH carry stamp 1 --- e.g., they were both extracted from
; the same single observation. The two A->D derivations inherit the shared
; stamp; StampDisjoint blocks the revision; confidence stays at single-path
; level.
(= (kb-collision)
   ((Sentence ((Inheritance A B) (stv 0.25 0.9)) (1))
    (Sentence ((Inheritance B D) (stv 0.5  0.9)) (2))
    (Sentence ((Inheritance A C) (stv 0.25 0.9)) (1))   ; <-- shares stamp 1
    (Sentence ((Inheritance C D) (stv 0.5  0.9)) (4))))

; ============================================================================
; Run Case 1: disjoint
; ============================================================================

!(println! "===========================================================")
!(println! "CASE 1 --- KB-disjoint  (four independent stamps)")
!(println! "===========================================================")
!(println! "")

!(println! "Trace (each SELECTED line is one engine step):")
!(println! (PLN.Query (kb-disjoint) (Inheritance A D) 25))
!(println! "")
!(println! "(Expected:  ((stv 0.5 0.9473684210526316) (1 2 3 4))")
!(println! "  --- confidence ~0.9474 from revision of two disjoint paths.)")
!(println! "")

; ============================================================================
; Run Case 2: collision
; ============================================================================

!(println! "===========================================================")
!(println! "CASE 2 --- KB-collision  (A->B and A->C share stamp 1)")
!(println! "===========================================================")
!(println! "")

!(println! "Trace (each SELECTED line is one engine step):")
!(println! (PLN.Query (kb-collision) (Inheritance A D) 25))
!(println! "")
!(println! "(Expected:  ((stv 0.5 0.9) (1 2)) or ((stv 0.5 0.9) (1 4))")
!(println! "  --- confidence STAYS AT 0.9 because StampDisjoint blocked")
!(println! "      the revision of the two A->D candidates that share")
!(println! "      stamp 1.  The system correctly refuses to double-count.)")
!(println! "")

!(println! "===========================================================")
!(println! "ABLATION SUMMARY")
!(println! "===========================================================")
!(println! "")
!(println! "The two cases differ ONLY in stamp assignment.  The numeric")
!(println! "TVs are identical at every input atom.  The confidence")
!(println! "difference in the output --- 0.9474 vs 0.9 --- is entirely")
!(println! "the StampDisjoint gate at lib_wmpln.metta:446 doing its job.")
!(println! "")
!(println! "This is the formal answer to 'why stamps matter':")
!(println! "without them, PLN's additive revision is unsound on")
!(println! "overlapping evidence.")
