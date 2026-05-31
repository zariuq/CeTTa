; ============================================================================
; OVERLAP-CORRECTED REVISION DEMO (CeTTa mirror)
; ============================================================================
;
; Mirror of hyperon/PeTTa/examples/overlap_corrected_revision_demo.metta.
; Same algebra, runs in CeTTa native evaluator.  Used as cross-runtime
; consistency oracle: the PeTTa and CeTTa outputs MUST agree numerically.
;
; Run:  cd /home/zar/claude/hyperon/CeTTa
;       ulimit -v 10485760 && ./cetta examples/overlap_corrected_revision_demo.metta
; ============================================================================

!(import! &self ../lib/lib_wmpln)
!(import! &self ../lib/lib_overlap_corrected_revision)

!(println! "===========================================================")
!(println! " OVERLAP-CORRECTED REVISION DEMO (CeTTa)")
!(println! "===========================================================")
!(println! "")

;; Case 1: Disjoint stamps
!(println! "Case 1: DISJOINT STAMPS  (1)(2) vs (3)(4)")
!(println! "  Naive Truth_Revision (no stamps):")
!(println! (Truth_Revision (stv 0.6 0.9) (stv 0.4 0.8)))
!(println! "  Level 1 OverlapCorrected (stamps disjoint):")
!(println! (Truth_OverlapCorrectedRevision (stv 0.6 0.9) (stv 0.4 0.8) (1 2) (3 4)))
!(println! "  Expected: identical to Truth_Revision (no overlap to correct).")
!(println! "  Stamp intersection size:")
!(println! (StampIntersectionCount (1 2) (3 4)))
!(println! "")

;; Case 2: Identical stamps
!(println! "Case 2: IDENTICAL STAMPS  (1 2) vs (1 2)")
!(println! "  Naive Truth_Revision (would inflate):")
!(println! (Truth_Revision (stv 0.6 0.9) (stv 0.4 0.8)))
!(println! "  Level 1 OverlapCorrected (full discount):")
!(println! (Truth_OverlapCorrectedRevision (stv 0.6 0.9) (stv 0.4 0.8) (1 2) (1 2)))
!(println! "  Expected: confidence stays at single-source level (no inflation).")
!(println! "")

;; Case 3: Partial overlap
!(println! "Case 3: PARTIAL OVERLAP  (1 2 3) vs (2 3 4)")
!(println! "  Naive Truth_Revision (no provenance):")
!(println! (Truth_Revision (stv 0.6 0.9) (stv 0.4 0.8)))
!(println! "  Level 1 OverlapCorrected:")
!(println! (Truth_OverlapCorrectedRevision (stv 0.6 0.9) (stv 0.4 0.8) (1 2 3) (2 3 4)))
!(println! "  Expected: between disjoint and identical.")
!(println! "")

;; Case 4: Audit variant
!(println! "Case 4: AUDIT variant  (1 2 3) vs (2 3 4)")
!(println! (Truth_OverlapCorrectedRevisionAudit (stv 0.6 0.9) (stv 0.4 0.8) (1 2 3) (2 3 4)))
!(println! "")

;; Sanity assertions
!(println! "Sanity assertions:")
!(assertEqual (StampIntersectionCount (1 2) (3 4)) 0)
!(assertEqual (StampIntersectionCount (1 2) (1 2)) 2)
!(assertEqual (StampIntersectionCount (1 2 3) (2 3 4)) 2)
!(assertEqual
   (Truth_OverlapCorrectedRevision (stv 0.6 0.9) (stv 0.4 0.8) (1 2) (3 4))
   (Truth_Revision (stv 0.6 0.9) (stv 0.4 0.8)))
!(println! "All sanity assertions passed.")
