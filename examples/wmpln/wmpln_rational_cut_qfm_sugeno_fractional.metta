!(import! &self ../../lib/lib_wmpln)

; Runnable shadow for exact-denominator fractional QFM/Sugeno rational cuts.
;
; Lean syntax:
;   predicateFuzzyForAllCountingCardinalityThresholdCut_*
;   predicateFuzzyThereExistsCountingCardinalityThresholdCut_*
;   predicateSugenoCountingCardinalityThresholdCut_*
;
; Hand model: a four-object crisp profile.  Let hits be the number of entries
; equal to 1 and misses = 4 - hits.
;
;   QFM ForAll score at eps=0        = hits / 4
;   QFM ThereExists score at eps=0   = 1 - misses / 4 = hits / 4
;   Sugeno counting score, crisp     = countingCapacity(ext P) = hits / 4
;
; Therefore, for the exact denominator N=4:
;
;   k / 4 <= score    iff    k <= hits.
;
; The expected values below are hand-derived from the displayed four-object
; profiles.  They are not engine echoes of the same function.

(= (absdiff $x $y) (if (< $x $y) (- $y $x) (- $x $y)))
(= (approx $x $y) (< (absdiff $x $y) 0.000001))
(= (is-ge $threshold $score) (or (< $threshold $score) (approx $threshold $score)))

(= (bool-to-real True) 1.0)
(= (bool-to-real False) 0.0)
(= (bool-to-count True) 1)
(= (bool-to-count False) 0)

(= (is-near-one $eps $x)
   (and (<= (- 1.0 $eps) $x) (<= $x 1.0)))
(= (is-near-zero $eps $x)
   (and (<= 0.0 $x) (<= $x $eps)))

(= (threshold4 $k) (/ $k 4.0))

(= (crisp-hit-count4 $a $b $c $d)
   (+ (+ (bool-to-count (== $a 1.0))
         (bool-to-count (== $b 1.0)))
      (+ (bool-to-count (== $c 1.0))
         (bool-to-count (== $d 1.0)))))

(= (crisp-miss-count4 $a $b $c $d)
   (- 4 (crisp-hit-count4 $a $b $c $d)))

(= (near-one-fraction4 $eps $a $b $c $d)
   (/ (+ (+ (bool-to-real (is-near-one $eps $a))
            (bool-to-real (is-near-one $eps $b)))
         (+ (bool-to-real (is-near-one $eps $c))
            (bool-to-real (is-near-one $eps $d))))
      4.0))

(= (near-zero-fraction4 $eps $a $b $c $d)
   (/ (+ (+ (bool-to-real (is-near-zero $eps $a))
            (bool-to-real (is-near-zero $eps $b)))
         (+ (bool-to-real (is-near-zero $eps $c))
            (bool-to-real (is-near-zero $eps $d))))
      4.0))

(= (qfm-forall-score4 $eps $a $b $c $d)
   (near-one-fraction4 $eps $a $b $c $d))

(= (qfm-there-exists-score4 $eps $a $b $c $d)
   (- 1.0 (near-zero-fraction4 $eps $a $b $c $d)))

(= (sugeno-counting-crisp-score4 $a $b $c $d)
   (/ (crisp-hit-count4 $a $b $c $d) 4.0))

(= (qfm-forall-cut4 $k $a $b $c $d)
   (is-ge (threshold4 $k) (qfm-forall-score4 0.0 $a $b $c $d)))

(= (qfm-there-exists-cut4 $k $a $b $c $d)
   (is-ge (threshold4 $k) (qfm-there-exists-score4 0.0 $a $b $c $d)))

(= (sugeno-counting-cut4 $k $a $b $c $d)
   (is-ge (threshold4 $k) (sugeno-counting-crisp-score4 $a $b $c $d)))

(= (cardinality-threshold-event4 $k $a $b $c $d)
   (<= $k (crisp-hit-count4 $a $b $c $d)))

; Main profile: 1,1,1,0 has hits=3 and misses=1.
; The exact-denominator threshold k=3 is accepted by all three encodings.
!(assertEqual (crisp-hit-count4 1.0 1.0 1.0 0.0) 3)
!(assertEqual (crisp-miss-count4 1.0 1.0 1.0 0.0) 1)
!(assertEqual (approx (qfm-forall-score4 0.0 1.0 1.0 1.0 0.0) 0.75) True)
!(assertEqual (approx (qfm-there-exists-score4 0.0 1.0 1.0 1.0 0.0) 0.75) True)
!(assertEqual (approx (sugeno-counting-crisp-score4 1.0 1.0 1.0 0.0) 0.75) True)
!(assertEqual (qfm-forall-cut4 3 1.0 1.0 1.0 0.0) True)
!(assertEqual (qfm-there-exists-cut4 3 1.0 1.0 1.0 0.0) True)
!(assertEqual (sugeno-counting-cut4 3 1.0 1.0 1.0 0.0) True)
!(assertEqual (cardinality-threshold-event4 3 1.0 1.0 1.0 0.0) True)

; Raising the exact-denominator threshold to k=4 rejects the same profile.
!(assertEqual (qfm-forall-cut4 4 1.0 1.0 1.0 0.0) False)
!(assertEqual (qfm-there-exists-cut4 4 1.0 1.0 1.0 0.0) False)
!(assertEqual (sugeno-counting-cut4 4 1.0 1.0 1.0 0.0) False)
!(assertEqual (cardinality-threshold-event4 4 1.0 1.0 1.0 0.0) False)

; Equality boundary: 1,1,0,0 has hits=2, so k=2 is accepted.
!(assertEqual (crisp-hit-count4 1.0 1.0 0.0 0.0) 2)
!(assertEqual (approx (qfm-forall-score4 0.0 1.0 1.0 0.0 0.0) 0.5) True)
!(assertEqual (approx (qfm-there-exists-score4 0.0 1.0 1.0 0.0 0.0) 0.5) True)
!(assertEqual (approx (sugeno-counting-crisp-score4 1.0 1.0 0.0 0.0) 0.5) True)
!(assertEqual (qfm-forall-cut4 2 1.0 1.0 0.0 0.0) True)
!(assertEqual (qfm-there-exists-cut4 2 1.0 1.0 0.0 0.0) True)
!(assertEqual (sugeno-counting-cut4 2 1.0 1.0 0.0 0.0) True)
!(assertEqual (cardinality-threshold-event4 2 1.0 1.0 0.0 0.0) True)

; One step above the boundary fails for the same two-hit profile.
!(assertEqual (qfm-forall-cut4 3 1.0 1.0 0.0 0.0) False)
!(assertEqual (qfm-there-exists-cut4 3 1.0 1.0 0.0 0.0) False)
!(assertEqual (sugeno-counting-cut4 3 1.0 1.0 0.0 0.0) False)
!(assertEqual (cardinality-threshold-event4 3 1.0 1.0 0.0 0.0) False)

; Empty and full endpoints agree with cardinality.
!(assertEqual (crisp-hit-count4 0.0 0.0 0.0 0.0) 0)
!(assertEqual (approx (qfm-forall-score4 0.0 0.0 0.0 0.0 0.0) 0.0) True)
!(assertEqual (approx (qfm-there-exists-score4 0.0 0.0 0.0 0.0 0.0) 0.0) True)
!(assertEqual (approx (sugeno-counting-crisp-score4 0.0 0.0 0.0 0.0) 0.0) True)
!(assertEqual (qfm-forall-cut4 1 0.0 0.0 0.0 0.0) False)
!(assertEqual (qfm-there-exists-cut4 1 0.0 0.0 0.0 0.0) False)
!(assertEqual (sugeno-counting-cut4 1 0.0 0.0 0.0 0.0) False)
!(assertEqual (cardinality-threshold-event4 1 0.0 0.0 0.0 0.0) False)

!(assertEqual (crisp-hit-count4 1.0 1.0 1.0 1.0) 4)
!(assertEqual (approx (qfm-forall-score4 0.0 1.0 1.0 1.0 1.0) 1.0) True)
!(assertEqual (approx (qfm-there-exists-score4 0.0 1.0 1.0 1.0 1.0) 1.0) True)
!(assertEqual (approx (sugeno-counting-crisp-score4 1.0 1.0 1.0 1.0) 1.0) True)
!(assertEqual (qfm-forall-cut4 4 1.0 1.0 1.0 1.0) True)
!(assertEqual (qfm-there-exists-cut4 4 1.0 1.0 1.0 1.0) True)
!(assertEqual (sugeno-counting-cut4 4 1.0 1.0 1.0 1.0) True)
!(assertEqual (cardinality-threshold-event4 4 1.0 1.0 1.0 1.0) True)

; Out-of-range guardrail: k=5 asks for threshold 5/4, impossible on a
; normalized four-object score.
!(assertEqual (approx (threshold4 5) 1.25) True)
!(assertEqual (qfm-forall-cut4 5 1.0 1.0 1.0 1.0) False)
!(assertEqual (qfm-there-exists-cut4 5 1.0 1.0 1.0 1.0) False)
!(assertEqual (sugeno-counting-cut4 5 1.0 1.0 1.0 1.0) False)
!(assertEqual (cardinality-threshold-event4 5 1.0 1.0 1.0 1.0) False)

; Exact-cardinality guardrail: the same raw hit count means a different score
; under a different carrier size.  The Lean theorem's |U|=4 hypothesis is
; therefore load-bearing.
!(assertEqual (approx (sugeno-counting-crisp-score4 1.0 1.0 0.0 0.0) 0.5) True)
!(assertEqual (approx (/ 2 2.0) 1.0) True)
!(assertEqual (approx (sugeno-counting-crisp-score4 1.0 1.0 0.0 0.0) (/ 2 2.0)) False)

; Fuzzy tolerance guardrail: eps>0 can change QFM ForAll by counting
; near-one fuzzy values, but the Sugeno/cardinality theorem above is the crisp
; exact-counting theorem.  This keeps the example from laundering fuzzy
; near-one behavior into crisp cardinality.
!(assertEqual (approx (qfm-forall-score4 0.2 1.0 0.85 0.81 0.0) 0.75) True)
!(assertEqual (crisp-hit-count4 1.0 0.85 0.81 0.0) 1)
!(assertEqual (qfm-forall-cut4 3 1.0 0.85 0.81 0.0) False)
!(assertEqual (is-ge (threshold4 3) (qfm-forall-score4 0.2 1.0 0.85 0.81 0.0)) True)
!(assertEqual (cardinality-threshold-event4 3 1.0 0.85 0.81 0.0) False)
