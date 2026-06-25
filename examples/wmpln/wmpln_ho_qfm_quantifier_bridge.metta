!(import! &self ../../lib/lib_wmpln)

; Runnable shadows for the HO-WM-PLN QFM / fuzzy-quantifier bridge.
;
; These examples mirror the Lean theorem surface in
; PLNHigherOrderHOLQuantifierBridge.lean and PLNHigherOrderHOLDefinableCuts.lean:
; - HOL predicates are consumed through the existing satisfying-set/QFM layer,
; - crisp ForAll at ε=0, PCL=1 is ordinary universal truth,
; - crisp ThereExists at ε=0, PCL=1 is the "no near-zero witness" endpoint,
;   hence it is also universal truth for crisp profiles, not ordinary ∃,
; - relaxed tolerance / PCL and interval checks stay semantic numeric envelopes.
;
; Expected values are hand-derived from four-object profiles.

(= (bool-to-real True) 1.0)
(= (bool-to-real False) 0.0)

(= (near-one? $eps $x)
   (and (<= (- 1.0 $eps) $x) (<= $x 1.0)))
(= (near-zero? $eps $x)
   (and (<= 0.0 $x) (<= $x $eps)))

(= (near-one-fraction4 $eps $a $b $c $d)
   (/ (+ (+ (bool-to-real (near-one? $eps $a))
            (bool-to-real (near-one? $eps $b)))
         (+ (bool-to-real (near-one? $eps $c))
            (bool-to-real (near-one? $eps $d))))
      4.0))

(= (near-zero-fraction4 $eps $a $b $c $d)
   (/ (+ (+ (bool-to-real (near-zero? $eps $a))
            (bool-to-real (near-zero? $eps $b)))
         (+ (bool-to-real (near-zero? $eps $c))
            (bool-to-real (near-zero? $eps $d))))
      4.0))

(= (qfm-forall-holds4 $eps $pcl $a $b $c $d)
   (<= $pcl (near-one-fraction4 $eps $a $b $c $d)))

(= (qfm-there-exists-holds4 $eps $pcl $a $b $c $d)
   (<= $pcl (- 1.0 (near-zero-fraction4 $eps $a $b $c $d))))

(= (qfm-exists-score4 $eps $a $b $c $d)
   (near-one-fraction4 $eps $a $b $c $d))

(= (qfm-interval-holds4 $eps $lpc $upc $a $b $c $d)
   (and (<= $lpc (near-one-fraction4 $eps $a $b $c $d))
        (<= (near-one-fraction4 $eps $a $b $c $d) $upc)))

; Strict endpoint: ε=0, PCL=1.
; All four witnesses true gives near-one fraction 1 and ForAll holds.
!(assertEqual (near-one-fraction4 0.0 1.0 1.0 1.0 1.0) 1.0)
!(assertEqual (qfm-forall-holds4 0.0 1.0 1.0 1.0 1.0 1.0) True)

; One crisp false witness drops the ForAll endpoint to 3/4, so PCL=1 rejects it.
!(assertEqual (near-one-fraction4 0.0 1.0 1.0 1.0 0.0) 0.75)
!(assertEqual (qfm-forall-holds4 0.0 1.0 1.0 1.0 1.0 0.0) False)

; The subtle PLN-2008 QFM endpoint:
; "ThereExists" at ε=0, PCL=1 means every witness is not near-zero.
; Thus a single true witness is ordinary existential truth, but NOT this
; maximal-threshold QFM endpoint.
(= (ordinary-exists4 $a $b $c $d)
   (or (== $a 1.0) (or (== $b 1.0) (or (== $c 1.0) (== $d 1.0)))))

!(assertEqual (ordinary-exists4 1.0 0.0 0.0 0.0) True)
!(assertEqual (near-zero-fraction4 0.0 1.0 0.0 0.0 0.0) 0.75)
!(assertEqual (qfm-there-exists-holds4 0.0 1.0 1.0 0.0 0.0 0.0) False)
!(assertEqual (qfm-there-exists-holds4 0.0 1.0 1.0 1.0 1.0 1.0) True)

; Relaxed finite-QFM profile:
; with ε=0.15, values 1.0, .92, .88 are near-one, .40 is not.
; The near-one fraction is 3/4, so PCL=.70 accepts and PCL=.80 rejects.
!(assertEqual (near-one-fraction4 0.15 1.0 0.92 0.88 0.40) 0.75)
!(assertEqual (qfm-forall-holds4 0.15 0.70 1.0 0.92 0.88 0.40) True)
!(assertEqual (qfm-forall-holds4 0.15 0.80 1.0 0.92 0.88 0.40) False)

; Interval-style Average/QFM check: the same 3/4 score lies in [0.70,0.80]
; but not in [0.80,1.00].
!(assertEqual (qfm-interval-holds4 0.15 0.70 0.80 1.0 0.92 0.88 0.40) True)
!(assertEqual (qfm-interval-holds4 0.15 0.80 1.00 1.0 0.92 0.88 0.40) False)

; Monotonicity under predicate inheritance / pointwise strengthening:
; upgrading the last value from .40 to .90 raises the near-one fraction from
; 3/4 to 1, so any accepted ForAll remains accepted and a stricter one may
; become accepted.
!(assertEqual (near-one-fraction4 0.15 1.0 0.92 0.88 0.90) 1.0)
!(assertEqual
  (<= (qfm-exists-score4 0.15 1.0 0.92 0.88 0.40)
      (qfm-exists-score4 0.15 1.0 0.92 0.88 0.90))
  True)
!(assertEqual (qfm-forall-holds4 0.15 0.80 1.0 0.92 0.88 0.90) True)

; Rational-cut witness for Boolean-valued finite-QFM endpoint acceptance:
; Lean constructors
;   predicateFuzzyForAllCrispEndpointPositiveThresholdCut
;   predicateFuzzyThereExistsCrispEndpointPositiveThresholdCut
; reuse the endpoint-1 QFM acceptance score, which is literally 0 or 1.
; Therefore every positive threshold 0 < theta <= 1 accepts exactly the same
; profiles as endpoint threshold 1.  Their nonpositive-threshold companions are
; tautological: theta <= 0 accepts even failed endpoint cases.
(= (qfm-accept-score $holds) (bool-to-real $holds))

(= (qfm-forall-positive-threshold-cut4 $theta $a $b $c $d)
   (<= $theta (qfm-accept-score (qfm-forall-holds4 0.0 1.0 $a $b $c $d))))

(= (qfm-there-exists-positive-threshold-cut4 $theta $a $b $c $d)
   (<= $theta (qfm-accept-score (qfm-there-exists-holds4 0.0 1.0 $a $b $c $d))))

(= (qfm-forall-nonpositive-threshold-cut4 $theta $a $b $c $d)
   (<= $theta (qfm-accept-score (qfm-forall-holds4 0.0 1.0 $a $b $c $d))))

(= (qfm-there-exists-nonpositive-threshold-cut4 $theta $a $b $c $d)
   (<= $theta (qfm-accept-score (qfm-there-exists-holds4 0.0 1.0 $a $b $c $d))))

; ForAll positive thresholds: all true gives Boolean score 1, so both
; theta=.40 and theta=1 accept.  One false gives score 0, so every strict
; positive threshold rejects.
!(assertEqual
  (qfm-accept-score (qfm-forall-holds4 0.0 1.0 1.0 1.0 1.0 1.0))
  1.0)
!(assertEqual
  (qfm-forall-positive-threshold-cut4 0.40 1.0 1.0 1.0 1.0)
  True)
!(assertEqual
  (qfm-forall-positive-threshold-cut4 1.0 1.0 1.0 1.0 1.0)
  True)
!(assertEqual
  (qfm-accept-score (qfm-forall-holds4 0.0 1.0 1.0 1.0 1.0 0.0))
  0.0)
!(assertEqual
  (qfm-forall-positive-threshold-cut4 0.40 1.0 1.0 1.0 0.0)
  False)
!(assertEqual
  (qfm-forall-positive-threshold-cut4 1.0 1.0 1.0 1.0 0.0)
  False)

; ThereExists endpoint positive thresholds use the PLN no-near-zero endpoint,
; not ordinary existential truth.  One true witness with three zeros is
; ordinary-exists true, but the endpoint score is 0 and strict positive
; thresholds reject.
!(assertEqual (ordinary-exists4 1.0 0.0 0.0 0.0) True)
!(assertEqual
  (qfm-accept-score (qfm-there-exists-holds4 0.0 1.0 1.0 0.0 0.0 0.0))
  0.0)
!(assertEqual
  (qfm-there-exists-positive-threshold-cut4 0.40 1.0 0.0 0.0 0.0)
  False)
!(assertEqual
  (qfm-there-exists-positive-threshold-cut4 1.0 1.0 0.0 0.0 0.0)
  False)
!(assertEqual
  (qfm-accept-score (qfm-there-exists-holds4 0.0 1.0 1.0 1.0 1.0 1.0))
  1.0)
!(assertEqual
  (qfm-there-exists-positive-threshold-cut4 0.40 1.0 1.0 1.0 1.0)
  True)

; Nonpositive-threshold boundary: theta=0 and theta<0 accept even failed
; endpoint cases.  This is why Lean represents the boundary by a tautology
; rather than by the original QFM endpoint formula.
!(assertEqual
  (qfm-forall-nonpositive-threshold-cut4 0.0 1.0 1.0 1.0 0.0)
  True)
!(assertEqual
  (qfm-forall-nonpositive-threshold-cut4 -0.25 1.0 1.0 1.0 0.0)
  True)
!(assertEqual
  (qfm-there-exists-nonpositive-threshold-cut4 0.0 1.0 0.0 0.0 0.0)
  True)
!(assertEqual
  (qfm-there-exists-nonpositive-threshold-cut4 -0.25 1.0 0.0 0.0 0.0)
  True)

; Strictness guardrail: just above zero is no longer tautological.
!(assertEqual
  (qfm-forall-positive-threshold-cut4 0.01 1.0 1.0 1.0 0.0)
  False)
!(assertEqual
  (qfm-there-exists-positive-threshold-cut4 0.01 1.0 0.0 0.0 0.0)
  False)
