; ============================================================================
; OVERLAP-CORRECTED REVISION (Level 1) for CeTTa WM-PLN
; ============================================================================
;
; Mirror of hyperon/PeTTa/lib/lib_overlap_corrected_revision.metta.  Identical
; formula, same semantics, runs in the CeTTa native evaluator.  The two are
; cross-runtime consistency oracles: same input -> same output.
;
; Formula (operational adaptation of
; lean-projects/mettapedia/Mettapedia/Logic/AbstractInheritanceOverlap.lean:140
; overlapEvidence):
;
;   per_pos_i = pos_i / |S_i|
;   per_neg_i = neg_i / |S_i|
;
;   overlap_pos = |S1 ∩ S2| * (per_pos_1 + per_pos_2) / 2
;   overlap_neg = |S1 ∩ S2| * (per_neg_1 + per_neg_2) / 2
;
;   merged_pos  = pos1 + pos2 - overlap_pos
;   merged_neg  = neg1 + neg2 - overlap_neg
;
;   merged_strength    = merged_pos / (merged_pos + merged_neg)
;   merged_confidence  = (merged_pos + merged_neg) / (merged_pos + merged_neg + 1)
;
; Sanity: stamps disjoint -> reduces to additive Truth_Revision.
;
; Honesty caveat: Level 1 graded heuristic, not a theorem from the Lean side.
; The Lean theorem covers the disjoint endpoint exactly; the graded
; interpolation is defensible but unproven without per-stamp evidence labels.
; ============================================================================

;; Stamp intersection: elements appearing in both tuples.
(= (StampIntersection $S1 $S2)
   (collapse (let* (($x (superpose $S1))
                    ($y (superpose $S2)))
                   (case (== $x $y) ((True $x))))))

;; Stamp intersection size.
(= (StampIntersectionCount $S1 $S2)
   (TupleCount (StampIntersection $S1 $S2)))

;; Per-stamp-element pos / neg contributions.
(= (PerStampPos $s $c $stampSize)
   (if (> $stampSize 0)
       (let* (($w (Truth_c2w $c))
              ($pos (* $s $w)))
             (/ $pos $stampSize))
       0))

(= (PerStampNeg $s $c $stampSize)
   (if (> $stampSize 0)
       (let* (($w (Truth_c2w $c))
              ($neg (* (- 1 $s) $w)))
             (/ $neg $stampSize))
       0))

;; Operational overlap-corrected revision.
(= (Truth_OverlapCorrectedRevision (stv $s1 $c1) (stv $s2 $c2) $S1 $S2)
   (let* (($w1 (Truth_c2w $c1))
          ($w2 (Truth_c2w $c2))
          ($pos1 (* $s1 $w1))
          ($neg1 (* (- 1 $s1) $w1))
          ($pos2 (* $s2 $w2))
          ($neg2 (* (- 1 $s2) $w2))
          ($size1 (TupleCount $S1))
          ($size2 (TupleCount $S2))
          ($shared (StampIntersectionCount $S1 $S2))
          ($per_pos_1 (PerStampPos $s1 $c1 $size1))
          ($per_pos_2 (PerStampPos $s2 $c2 $size2))
          ($per_neg_1 (PerStampNeg $s1 $c1 $size1))
          ($per_neg_2 (PerStampNeg $s2 $c2 $size2))
          ($shared_pos (* $shared (/ (+ $per_pos_1 $per_pos_2) 2)))
          ($shared_neg (* $shared (/ (+ $per_neg_1 $per_neg_2) 2)))
          ($naive_pos (+ $pos1 $pos2))
          ($naive_neg (+ $neg1 $neg2))
          ($merged_pos (if (> $naive_pos $shared_pos) (- $naive_pos $shared_pos) 0))
          ($merged_neg (if (> $naive_neg $shared_neg) (- $naive_neg $shared_neg) 0))
          ($merged_total (+ $merged_pos $merged_neg))
          ($merged_s (if (> $merged_total 0) (/ $merged_pos $merged_total) 0))
          ($merged_c (Truth_w2c $merged_total)))
         (stv (min 1.0 $merged_s) (min 1.0 $merged_c))))

;; Audit variant: merged TV + provenance metadata for honesty policies.
(= (Truth_OverlapCorrectedRevisionAudit (stv $s1 $c1) (stv $s2 $c2) $S1 $S2)
   (let* (($merged (Truth_OverlapCorrectedRevision (stv $s1 $c1) (stv $s2 $c2) $S1 $S2))
          ($shared (StampIntersectionCount $S1 $S2))
          ($size1 (TupleCount $S1))
          ($size2 (TupleCount $S2)))
         (overlap-corrected
            (merged $merged)
            (stamp1-size $size1)
            (stamp2-size $size2)
            (shared $shared)
            (stamp1 $S1)
            (stamp2 $S2)
            (stamp-union (StampConcat $S1 $S2)))))
