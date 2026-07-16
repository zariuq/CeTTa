!(import! &self ../../lib/lib_wmpln)

; Runnable shadow for the arbitrary-k base-counting rational-cut capstone.
;
; Lean surface:
;   predicateFuzzyForAllCountingAtLeastNBaseCut_width_eq_zero_iff_decides
;   predicateFuzzyThereExistsCountingAtLeastNBaseCut_width_eq_zero_iff_decides
;   predicateCountingCapacityAtLeastNBaseCut_width_eq_zero_iff_decides
;
; Hand model: a five-object finite carrier, with `hits` objects in ext(P).
; At eps=0 on crisp predicates:
;   QFM ForAll score        = hits / 5
;   QFM ThereExists score   = 1 - misses / 5 = hits / 5
;   counting capacity score = hits / 5
;
; Therefore, for the exact denominator N=5:
;   k / 5 <= score    iff    k <= hits.
;
; These are example witnesses, not regression tests and not proof oracles.  The
; expected values below are hand-derived from the displayed five-object
; profiles; the Lean theorem is the proof authority.

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

(= (threshold5 $k) (/ $k 5.0))

(= (crisp-hit-count5 $a $b $c $d $e)
   (+ (+ (bool-to-count (== $a 1.0))
         (bool-to-count (== $b 1.0)))
      (+ (+ (bool-to-count (== $c 1.0))
            (bool-to-count (== $d 1.0)))
         (bool-to-count (== $e 1.0)))))

(= (crisp-miss-count5 $a $b $c $d $e)
   (- 5 (crisp-hit-count5 $a $b $c $d $e)))

(= (near-one-fraction5 $eps $a $b $c $d $e)
   (/ (+ (+ (bool-to-real (is-near-one $eps $a))
            (bool-to-real (is-near-one $eps $b)))
         (+ (+ (bool-to-real (is-near-one $eps $c))
               (bool-to-real (is-near-one $eps $d)))
            (bool-to-real (is-near-one $eps $e))))
      5.0))

(= (near-zero-fraction5 $eps $a $b $c $d $e)
   (/ (+ (+ (bool-to-real (is-near-zero $eps $a))
            (bool-to-real (is-near-zero $eps $b)))
         (+ (+ (bool-to-real (is-near-zero $eps $c))
               (bool-to-real (is-near-zero $eps $d)))
            (bool-to-real (is-near-zero $eps $e))))
      5.0))

(= (qfm-forall-score5 $eps $a $b $c $d $e)
   (near-one-fraction5 $eps $a $b $c $d $e))

(= (qfm-there-exists-score5 $eps $a $b $c $d $e)
   (- 1.0 (near-zero-fraction5 $eps $a $b $c $d $e)))

(= (counting-capacity-score5 $a $b $c $d $e)
   (/ (crisp-hit-count5 $a $b $c $d $e) 5.0))

(= (qfm-forall-atleastN-cut5 $k $a $b $c $d $e)
   (is-ge (threshold5 $k) (qfm-forall-score5 0.0 $a $b $c $d $e)))

(= (qfm-there-exists-atleastN-cut5 $k $a $b $c $d $e)
   (is-ge (threshold5 $k) (qfm-there-exists-score5 0.0 $a $b $c $d $e)))

(= (counting-capacity-atleastN-cut5 $k $a $b $c $d $e)
   (is-ge (threshold5 $k) (counting-capacity-score5 $a $b $c $d $e)))

(= (cardinality-atleastN-event5 $k $a $b $c $d $e)
   (<= $k (crisp-hit-count5 $a $b $c $d $e)))

(= (all-three-capstone-cuts5 $k $a $b $c $d $e)
   (and (qfm-forall-atleastN-cut5 $k $a $b $c $d $e)
        (and (qfm-there-exists-atleastN-cut5 $k $a $b $c $d $e)
             (counting-capacity-atleastN-cut5 $k $a $b $c $d $e))))

; Positive arbitrary-k case:
; profile 1,1,1,0,1 has hits=4 and misses=1.
; k=4 gives threshold 4/5, so all three numeric surfaces accept exactly on the
; constructed HOL cardinality event "at least 4 witnesses".
!(assertEqual (crisp-hit-count5 1.0 1.0 1.0 0.0 1.0) 4)
!(assertEqual (crisp-miss-count5 1.0 1.0 1.0 0.0 1.0) 1)
!(assertEqual (approx (threshold5 4) 0.8) True)
!(assertEqual (approx (qfm-forall-score5 0.0 1.0 1.0 1.0 0.0 1.0) 0.8) True)
!(assertEqual (approx (qfm-there-exists-score5 0.0 1.0 1.0 1.0 0.0 1.0) 0.8) True)
!(assertEqual (approx (counting-capacity-score5 1.0 1.0 1.0 0.0 1.0) 0.8) True)
!(assertEqual (qfm-forall-atleastN-cut5 4 1.0 1.0 1.0 0.0 1.0) True)
!(assertEqual (qfm-there-exists-atleastN-cut5 4 1.0 1.0 1.0 0.0 1.0) True)
!(assertEqual (counting-capacity-atleastN-cut5 4 1.0 1.0 1.0 0.0 1.0) True)
!(assertEqual (cardinality-atleastN-event5 4 1.0 1.0 1.0 0.0 1.0) True)
!(assertEqual (all-three-capstone-cuts5 4 1.0 1.0 1.0 0.0 1.0) True)

; Negative one-step-above case:
; the same profile has only four hits, so k=5 accepts only if all five objects
; satisfy P.  The numeric threshold is 1.0, and all three surfaces reject.
!(assertEqual (approx (threshold5 5) 1.0) True)
!(assertEqual (qfm-forall-atleastN-cut5 5 1.0 1.0 1.0 0.0 1.0) False)
!(assertEqual (qfm-there-exists-atleastN-cut5 5 1.0 1.0 1.0 0.0 1.0) False)
!(assertEqual (counting-capacity-atleastN-cut5 5 1.0 1.0 1.0 0.0 1.0) False)
!(assertEqual (cardinality-atleastN-event5 5 1.0 1.0 1.0 0.0 1.0) False)
!(assertEqual (all-three-capstone-cuts5 5 1.0 1.0 1.0 0.0 1.0) False)

; Equality boundary:
; profile 1,0,1,0,1 has hits=3.  k=3 is accepted by exact equality at
; threshold 3/5; k=4 rejects.
!(assertEqual (crisp-hit-count5 1.0 0.0 1.0 0.0 1.0) 3)
!(assertEqual (approx (threshold5 3) 0.6) True)
!(assertEqual (approx (qfm-forall-score5 0.0 1.0 0.0 1.0 0.0 1.0) 0.6) True)
!(assertEqual (approx (qfm-there-exists-score5 0.0 1.0 0.0 1.0 0.0 1.0) 0.6) True)
!(assertEqual (approx (counting-capacity-score5 1.0 0.0 1.0 0.0 1.0) 0.6) True)
!(assertEqual (all-three-capstone-cuts5 3 1.0 0.0 1.0 0.0 1.0) True)
!(assertEqual (cardinality-atleastN-event5 3 1.0 0.0 1.0 0.0 1.0) True)
!(assertEqual (all-three-capstone-cuts5 4 1.0 0.0 1.0 0.0 1.0) False)
!(assertEqual (cardinality-atleastN-event5 4 1.0 0.0 1.0 0.0 1.0) False)

; Empty and full endpoints.
!(assertEqual (crisp-hit-count5 0.0 0.0 0.0 0.0 0.0) 0)
!(assertEqual (approx (qfm-forall-score5 0.0 0.0 0.0 0.0 0.0 0.0) 0.0) True)
!(assertEqual (approx (qfm-there-exists-score5 0.0 0.0 0.0 0.0 0.0 0.0) 0.0) True)
!(assertEqual (approx (counting-capacity-score5 0.0 0.0 0.0 0.0 0.0) 0.0) True)
!(assertEqual (all-three-capstone-cuts5 1 0.0 0.0 0.0 0.0 0.0) False)
!(assertEqual (cardinality-atleastN-event5 1 0.0 0.0 0.0 0.0 0.0) False)

!(assertEqual (crisp-hit-count5 1.0 1.0 1.0 1.0 1.0) 5)
!(assertEqual (approx (qfm-forall-score5 0.0 1.0 1.0 1.0 1.0 1.0) 1.0) True)
!(assertEqual (approx (qfm-there-exists-score5 0.0 1.0 1.0 1.0 1.0 1.0) 1.0) True)
!(assertEqual (approx (counting-capacity-score5 1.0 1.0 1.0 1.0 1.0) 1.0) True)
!(assertEqual (all-three-capstone-cuts5 5 1.0 1.0 1.0 1.0 1.0) True)
!(assertEqual (cardinality-atleastN-event5 5 1.0 1.0 1.0 1.0 1.0) True)

; Out-of-range guardrail: k=6 asks for threshold 6/5, impossible on a
; normalized five-object score and impossible as a cardinality event.
!(assertEqual (approx (threshold5 6) 1.2) True)
!(assertEqual (all-three-capstone-cuts5 6 1.0 1.0 1.0 1.0 1.0) False)
!(assertEqual (cardinality-atleastN-event5 6 1.0 1.0 1.0 1.0 1.0) False)

; Exact-cardinality guardrail:
; the same raw hit count means different scores under a different denominator.
; Four hits over five objects is 0.8; four hits over four objects would be 1.0.
; The Lean theorem's fixed carrier-cardinality hypothesis is load-bearing.
!(assertEqual (approx (counting-capacity-score5 1.0 1.0 1.0 0.0 1.0) 0.8) True)
!(assertEqual (approx (/ 4 4.0) 1.0) True)
!(assertEqual (approx (counting-capacity-score5 1.0 1.0 1.0 0.0 1.0) (/ 4 4.0)) False)
