!(import! &self ../../lib/lib_wmpln)
!(import! &self ../../lib/lib_overlap_corrected_revision)

; Runnable witness for the Revision provenance boundary proved in
; AbstractInheritanceOverlap.lean.
;
; The expectations below are hand-derived from evidence weights, not copied from
; the engine.  For (stv 0.6 0.9), weight=9, so pos=5.4 and neg=3.6.  For
; (stv 0.4 0.8), weight=4, so pos=1.6 and neg=2.4.

; Guarded additive Revision is allowed exactly in the zero-overlap case.  The
; overlap-corrected merge conservatively extends that guarded syntax: when the
; guard allows, corrected revision agrees with raw revision; when the guard
; rejects, corrected revision remains available and avoids double-counting.
(= (GuardedRevisionAllows $stampA $stampB)
   (== (StampIntersectionCount $stampA $stampB) 0))

(= (PairwiseGuardedRevisionAllows3 $stampA $stampB $stampC)
   (and (GuardedRevisionAllows $stampA $stampB)
        (and (GuardedRevisionAllows $stampA $stampC)
             (GuardedRevisionAllows $stampB $stampC))))

; Positive case: disjoint stamps have zero overlap, so corrected revision agrees
; with ordinary evidence-weight revision.
!(assertEqual (StampIntersectionCount (1 2) (3 4)) 0)
!(assertEqual (GuardedRevisionAllows (1 2) (3 4)) True)
!(assertEqual
   (Truth_OverlapCorrectedRevision (stv 0.6 0.9) (stv 0.4 0.8) (1 2) (3 4))
   (Truth_Revision (stv 0.6 0.9) (stv 0.4 0.8)))

; Negative/boundary case: identical stamps mean all two stamp slots are shared.
; shared_pos = 2 * ((5.4/2 + 1.6/2)/2) = 3.5
; shared_neg = 2 * ((3.6/2 + 2.4/2)/2) = 3.0
; merged_pos = 7.0 - 3.5 = 3.5
; merged_neg = 6.0 - 3.0 = 3.0
; strength = 3.5 / 6.5 = 7/13
; confidence = 6.5 / 7.5 = 13/15
!(assertEqual (StampIntersectionCount (1 2) (1 2)) 2)
!(assertEqual (GuardedRevisionAllows (1 2) (1 2)) False)
!(assertEqual
   (Truth_OverlapCorrectedRevision (stv 0.6 0.9) (stv 0.4 0.8) (1 2) (1 2))
   (stv 0.5384615384615384 0.8666666666666667))

; Raw revision would inflate confidence by pretending the duplicate provenance
; is independent.  This checks the old over-counting behavior is not what the
; provenance-aware operator returns.
!(assertEqual
   (== (Truth_OverlapCorrectedRevision (stv 0.6 0.9) (stv 0.4 0.8) (1 2) (1 2))
       (Truth_Revision (stv 0.6 0.9) (stv 0.4 0.8)))
   False)

; Partial overlap keeps one foot in each world: it is neither disjoint raw
; revision nor the identical-stamp full correction.
!(assertEqual (StampIntersectionCount (1 2 3) (2 3 4)) 2)
!(assertEqual
   (== (Truth_OverlapCorrectedRevision
         (stv 0.6 0.9) (stv 0.4 0.8) (1 2 3) (2 3 4))
       (Truth_Revision (stv 0.6 0.9) (stv 0.4 0.8)))
   False)
!(assertEqual
   (== (Truth_OverlapCorrectedRevision
         (stv 0.6 0.9) (stv 0.4 0.8) (1 2 3) (2 3 4))
       (Truth_OverlapCorrectedRevision
         (stv 0.6 0.9) (stv 0.4 0.8) (1 2) (1 2)))
   False)

; Exact source-level stamp-set case mirroring
; correctedMerge_stampSetPacket_eq_union: all-positive evidence with one unit
; per stamp merges to evidence over the stamp union.
;
; Three positive stamps have weight 3 and confidence 3/4.  The partial-overlap
; union of (1 2 3) and (2 3 4) has four distinct source stamps, so the corrected
; merge has weight 4 and confidence 4/5.
!(assertEqual
   (Truth_OverlapCorrectedRevision (stv 1.0 0.75) (stv 1.0 0.75)
                                   (1 2 3) (2 3 4))
   (stv 1.0 0.8))

; The identical-source case stays at one three-stamp packet rather than
; inflating to six units of evidence.
!(assertEqual
   (Truth_OverlapCorrectedRevision (stv 1.0 0.75) (stv 1.0 0.75)
                                   (1 2 3) (1 2 3))
   (stv 1.0 0.75))
!(assertEqual
   (== (Truth_OverlapCorrectedRevision (stv 1.0 0.75) (stv 1.0 0.75)
                                       (1 2 3) (1 2 3))
       (Truth_Revision (stv 1.0 0.75) (stv 1.0 0.75)))
   False)

; N-ary joint-source reading: merge sources, then count the union.  Three
; all-positive three-stamp packets with overlaps
;   (1 2 3), (3 4 5), (5)
; represent five distinct source stamps in total, so the source-level joint
; packet has confidence 5/6.  This hand-derived target mirrors
; stampSetPacketJointMerge_eq_union rather than chaining truth-value revision
; as if the intermediate provenance had been forgotten.
!(assertEqual
   (Truth_OverlapCorrectedRevision (stv 1.0 0.75) (stv 1.0 0.75)
                                   (1 2 3) (3 4 5))
   (stv 1.0 0.8333333333333334))
!(assertEqual
   (Truth_OverlapCorrectedRevision (stv 1.0 0.8333333333333334) (stv 1.0 0.5)
                                   (1 2 3 4 5) (5))
   (stv 1.0 0.8333333333333334))
!(assertEqual
   (== (Truth_Revision (stv 1.0 0.75) (stv 1.0 0.75))
       (stv 1.0 0.8333333333333334))
   False)

; N-ary conservative-extension case mirroring
; stampSetPacketJointMerge_eq_of_guardedListRevise_eq_some: when three exact
; source packets are pairwise disjoint, guarded list Revision succeeds and the
; overlap-corrected source-union merge agrees with raw additive Revision.
; Counts 2 + 1 + 2 = 5, so the all-positive aggregate is stv(1,5/6).
!(assertEqual (PairwiseGuardedRevisionAllows3 (1 2) (3) (4 5)) True)
!(assertEqual
   (Truth_OverlapCorrectedRevision
     (Truth_OverlapCorrectedRevision (stv 1.0 0.6666666666666666)
                                     (stv 1.0 0.5)
                                     (1 2) (3))
     (stv 1.0 0.6666666666666666)
     (1 2 3) (4 5))
   (Truth_Revision
     (Truth_Revision (stv 1.0 0.6666666666666666)
                     (stv 1.0 0.5))
     (stv 1.0 0.6666666666666666)))
!(assertEqual
   (Truth_OverlapCorrectedRevision
     (Truth_OverlapCorrectedRevision (stv 1.0 0.6666666666666666)
                                     (stv 1.0 0.5)
                                     (1 2) (3))
     (stv 1.0 0.6666666666666666)
     (1 2 3) (4 5))
   (stv 1.0 0.8333333333333334))

; Negative n-ary boundary: one shared source stamp means guarded list Revision
; would reject the aggregate, and overlap-corrected source-union merging no
; longer agrees with raw additive chaining.
!(assertEqual (PairwiseGuardedRevisionAllows3 (1 2) (2 3) (4)) False)
!(assertEqual
   (== (Truth_OverlapCorrectedRevision
         (Truth_OverlapCorrectedRevision (stv 1.0 0.6666666666666666)
                                         (stv 1.0 0.6666666666666666)
                                         (1 2) (2 3))
         (stv 1.0 0.5)
         (1 2 3) (4))
       (Truth_Revision
         (Truth_Revision (stv 1.0 0.6666666666666666)
                         (stv 1.0 0.6666666666666666))
         (stv 1.0 0.5)))
   False)
