!(import! &self ../../lib/lib_wmpln)

; Runnable shadow for the positive-affine definable-cut transport theorem.
;
; The Lean theorem is
;   ExtensionalDefinableCut.posAffineRescale
; in PLNHigherOrderHOLDefinableCuts.lean.  It says that if a threshold event is
; already represented by a closed HOL formula, then a positive affine
; calibration x |-> a*x + b preserves that represented event at the transformed
; threshold a*t + b.  This is the safe algebraic part of general rational cuts;
; it does not make arbitrary numeric observables definable.

(= (absdiff $x $y) (if (< $x $y) (- $y $x) (- $x $y)))
(= (approx $x $y) (< (absdiff $x $y) 0.000001))

(= (affine-score $a $b $x) (+ (* $a $x) $b))
(= (is-ge $threshold $x) (or (< $threshold $x) (approx $threshold $x)))
(= (cut-event $a $b $threshold $raw) (is-ge $threshold (affine-score $a $b $raw)))
(= (formula-event $raw) (== $raw 1.0))
(= (is-same-event $a $b $threshold $raw)
   (== (formula-event $raw) (cut-event $a $b $threshold $raw)))

; Positive affine calibration #1:
; raw threshold t=1, a=.7, b=.2 gives transformed threshold .9.
; raw true:  .7*1+.2 = .9, so the transformed cut accepts.
; raw false: .7*0+.2 = .2, so the transformed cut rejects.
!(assertEqual (approx (affine-score 0.7 0.2 1.0) 0.9) True)
!(assertEqual (approx (affine-score 0.7 0.2 0.0) 0.2) True)
!(assertEqual (cut-event 0.7 0.2 0.9 1.0) True)
!(assertEqual (cut-event 0.7 0.2 0.9 0.0) False)
!(assertEqual (is-same-event 0.7 0.2 0.9 1.0) True)
!(assertEqual (is-same-event 0.7 0.2 0.9 0.0) True)

; Positive affine calibration #2, with a different non-1 threshold:
; raw threshold t=1, a=.5, b=.1 gives transformed threshold .6.
!(assertEqual (approx (affine-score 0.5 0.1 1.0) 0.6) True)
!(assertEqual (approx (affine-score 0.5 0.1 0.0) 0.1) True)
!(assertEqual (cut-event 0.5 0.1 0.6 1.0) True)
!(assertEqual (cut-event 0.5 0.1 0.6 0.0) False)

; Negative guardrail:
; if the scale is negative, the transformed threshold is not same-event
; preserving.  With a=-.7, b=1, transformed threshold .3 accepts both raw true
; (.3 >= .3) and raw false (1 >= .3), so it no longer represents the formula.
!(assertEqual (approx (affine-score -0.7 1.0 1.0) 0.3) True)
!(assertEqual (approx (affine-score -0.7 1.0 0.0) 1.0) True)
!(assertEqual (cut-event -0.7 1.0 0.3 1.0) True)
!(assertEqual (cut-event -0.7 1.0 0.3 0.0) True)
!(assertEqual (is-same-event -0.7 1.0 0.3 0.0) False)

; Boolean closure of certified cut events:
; The Lean constructors ExtensionalDefinableCut.andCut / orCut use signed
; margins.  Joint satisfaction is min(margin1, margin2) >= 0, while
; alternative satisfaction is max(margin1, margin2) >= 0.

(= (min2 $x $y) (if (< $x $y) $x $y))
(= (max2 $x $y) (if (< $x $y) $y $x))
(= (margin $threshold $score) (- $score $threshold))
(= (and-cut-score $t1 $s1 $t2 $s2)
   (min2 (margin $t1 $s1) (margin $t2 $s2)))
(= (or-cut-score $t1 $s1 $t2 $s2)
   (max2 (margin $t1 $s1) (margin $t2 $s2)))
(= (and-cut-event $t1 $s1 $t2 $s2)
   (is-ge 0.0 (and-cut-score $t1 $s1 $t2 $s2)))
(= (or-cut-event $t1 $s1 $t2 $s2)
   (is-ge 0.0 (or-cut-score $t1 $s1 $t2 $s2)))
(= (not-cut-score $threshold $score)
   (if (is-ge $threshold $score) 0.0 1.0))
(= (not-cut-event $threshold $score)
   (is-ge 1.0 (not-cut-score $threshold $score)))
(= (imp-cut-score $t1 $s1 $t2 $s2)
   (if (is-ge $t1 $s1)
       (if (is-ge $t2 $s2) 1.0 0.0)
       1.0))
(= (imp-cut-event $t1 $s1 $t2 $s2)
   (is-ge 1.0 (imp-cut-score $t1 $s1 $t2 $s2)))

; Both cuts pass: margins are .1 and .05, so min=.05 and max=.1.
!(assertEqual (approx (and-cut-score 0.6 0.7 0.8 0.85) 0.05) True)
!(assertEqual (approx (or-cut-score 0.6 0.7 0.8 0.85) 0.10) True)
!(assertEqual (and-cut-event 0.6 0.7 0.8 0.85) True)
!(assertEqual (or-cut-event 0.6 0.7 0.8 0.85) True)

; One cut fails: conjunction rejects, disjunction accepts.
!(assertEqual (approx (and-cut-score 0.6 0.7 0.8 0.7) -0.10) True)
!(assertEqual (approx (or-cut-score 0.6 0.7 0.8 0.7) 0.10) True)
!(assertEqual (and-cut-event 0.6 0.7 0.8 0.7) False)
!(assertEqual (or-cut-event 0.6 0.7 0.8 0.7) True)

; Both cuts fail: both Boolean closures reject.
!(assertEqual (and-cut-event 0.6 0.5 0.8 0.7) False)
!(assertEqual (or-cut-event 0.6 0.5 0.8 0.7) False)

; Boundary is inclusive: exact threshold satisfaction still passes the joint cut.
!(assertEqual (approx (and-cut-score 0.6 0.6 0.8 0.8) 0.0) True)
!(assertEqual (and-cut-event 0.6 0.6 0.8 0.8) True)

; Complement closure of certified cuts:
; The Lean constructor ExtensionalDefinableCut.notCut uses a Boolean indicator,
; not a signed margin, because the exact complement of threshold <= score is
; strict failure.  Equality therefore stays on the original event side.
!(assertEqual (approx (not-cut-score 0.6 0.7) 0.0) True)
!(assertEqual (not-cut-event 0.6 0.7) False)
!(assertEqual (approx (not-cut-score 0.6 0.5) 1.0) True)
!(assertEqual (not-cut-event 0.6 0.5) True)
!(assertEqual (is-ge 0.6 0.6) True)
!(assertEqual (not-cut-event 0.6 0.6) False)

; Implication closure of certified cuts:
; The Lean constructor ExtensionalDefinableCut.impCut represents
; premise-cut => conclusion-cut by HOL implication.  It is true when both pass,
; true vacuously when the premise fails, and false only when the premise passes
; while the conclusion fails.  Equality remains on the passing side.
!(assertEqual (approx (imp-cut-score 0.6 0.7 0.8 0.85) 1.0) True)
!(assertEqual (imp-cut-event 0.6 0.7 0.8 0.85) True)
!(assertEqual (approx (imp-cut-score 0.6 0.5 0.8 0.7) 1.0) True)
!(assertEqual (imp-cut-event 0.6 0.5 0.8 0.7) True)
!(assertEqual (approx (imp-cut-score 0.6 0.7 0.8 0.7) 0.0) True)
!(assertEqual (imp-cut-event 0.6 0.7 0.8 0.7) False)
!(assertEqual (imp-cut-event 0.6 0.6 0.8 0.8) True)
!(assertEqual (imp-cut-event 0.6 0.6 0.8 0.79) False)

; Certified-cut modus tollens:
; If the implication cut is true and the conclusion cut fails, then the premise
; cut must fail.  Pointwise shadow of
;   ExtensionalDefinableCut.notCut_lower_eq_one_of_impCut_lower_eq_one_of_notCut_lower_eq_one
; with premise .5 < .6 and conclusion .7 < .8:
!(assertEqual (imp-cut-event 0.6 0.5 0.8 0.7) True)
!(assertEqual (not-cut-event 0.8 0.7) True)
!(assertEqual (not-cut-event 0.6 0.5) True)
; Negative guardrail: when the premise passes and conclusion fails, the rule cut
; itself is false, so modus tollens cannot be used to infer failed premise.
!(assertEqual (imp-cut-event 0.6 0.7 0.8 0.7) False)
!(assertEqual (not-cut-event 0.8 0.7) True)
!(assertEqual (not-cut-event 0.6 0.7) False)

; Certified implication-cut refutation:
; If the premise is certainly above threshold and the conclusion is certainly
; below threshold, then the implication cut is refuted.  Pointwise shadow of
;   ExtensionalDefinableCut.impCut_upper_eq_zero_of_lower_eq_one_of_upper_eq_zero
!(assertEqual (cut-event 1.0 -0.4 0.6 1.0) True)  ; raw premise maps to .6
!(assertEqual (not-cut-event 0.8 0.7) True)
!(assertEqual (imp-cut-event 0.6 0.6 0.8 0.7) False)
; Boundary guardrail: equality on the conclusion stays on the passing side, so
; the implication is not refuted.
!(assertEqual (not-cut-event 0.8 0.8) False)
!(assertEqual (imp-cut-event 0.6 0.6 0.8 0.8) True)

; Nonempty finite certified gates:
; The Lean constructor ExtensionalDefinableCut.allCut folds a head certified
; cut with a finite list of already-certified cuts by repeated andCut.  For
; three premises, the pointwise score is the minimum of all signed margins, and
; the threshold is 0.  Thus the folded gate holds exactly when every premise
; threshold event holds.
(= (all3-cut-score $t1 $s1 $t2 $s2 $t3 $s3)
   (min2 (min2 (margin $t1 $s1) (margin $t2 $s2))
         (margin $t3 $s3)))
(= (all3-cut-event $t1 $s1 $t2 $s2 $t3 $s3)
   (is-ge 0.0 (all3-cut-score $t1 $s1 $t2 $s2 $t3 $s3)))
(= (all3-conjunction-event $t1 $s1 $t2 $s2 $t3 $s3)
   (and (is-ge $t1 $s1)
        (and (is-ge $t2 $s2)
             (is-ge $t3 $s3))))

; All three pass: margins .10, .05, .10, so the folded score is .05.
!(assertEqual (approx (all3-cut-score 0.6 0.7 0.8 0.85 0.4 0.5) 0.05) True)
!(assertEqual (all3-cut-event 0.6 0.7 0.8 0.85 0.4 0.5) True)
!(assertEqual (all3-conjunction-event 0.6 0.7 0.8 0.85 0.4 0.5) True)
!(assertEqual
  (== (all3-cut-event 0.6 0.7 0.8 0.85 0.4 0.5)
      (all3-conjunction-event 0.6 0.7 0.8 0.85 0.4 0.5))
  True)

; One tail cut fails: the folded score is negative and the conjunction rejects.
!(assertEqual (approx (all3-cut-score 0.6 0.7 0.8 0.85 0.4 0.39) -0.01) True)
!(assertEqual (all3-cut-event 0.6 0.7 0.8 0.85 0.4 0.39) False)
!(assertEqual (all3-conjunction-event 0.6 0.7 0.8 0.85 0.4 0.39) False)

; Boundary is inclusive for every premise.
!(assertEqual (approx (all3-cut-score 0.6 0.6 0.8 0.8 0.4 0.4) 0.0) True)
!(assertEqual (all3-cut-event 0.6 0.6 0.8 0.8 0.4 0.4) True)
!(assertEqual (all3-conjunction-event 0.6 0.6 0.8 0.8 0.4 0.4) True)

; Finite-gate implication:
; The Lean theorem
;   ExtensionalDefinableCut.allCut_impCut_lower_eq_one_iff_forall_all_imp_ge
; reads the implication from the finite allCut gate to a certified conclusion.
; Pointwise, this is implication from all3-cut-event to the conclusion cut.
(= (all3-imp-cut-score $t1 $s1 $t2 $s2 $t3 $s3 $tc $sc)
   (imp-cut-score 0.0 (all3-cut-score $t1 $s1 $t2 $s2 $t3 $s3) $tc $sc))
(= (all3-imp-cut-event $t1 $s1 $t2 $s2 $t3 $s3 $tc $sc)
   (is-ge 1.0 (all3-imp-cut-score $t1 $s1 $t2 $s2 $t3 $s3 $tc $sc)))

; All premises pass and the conclusion passes: implication holds.
!(assertEqual
  (all3-imp-cut-event 0.6 0.7 0.8 0.85 0.4 0.5 0.9 0.95)
  True)

; A premise fails and the conclusion fails: implication holds vacuously.
!(assertEqual
  (all3-cut-event 0.6 0.7 0.8 0.85 0.4 0.39)
  False)
!(assertEqual
  (all3-imp-cut-event 0.6 0.7 0.8 0.85 0.4 0.39 0.9 0.8)
  True)

; All premises pass but the conclusion fails: this is the finite-gate
; counterexample case, so the implication cut rejects.
!(assertEqual
  (all3-cut-event 0.6 0.7 0.8 0.85 0.4 0.5)
  True)
!(assertEqual
  (all3-imp-cut-event 0.6 0.7 0.8 0.85 0.4 0.5 0.9 0.8)
  False)

; Multi-premise certified-cut modus ponens:
; If the finite gate holds and its implication to the conclusion holds, then
; the conclusion cut holds.  The two negative guardrails show why both
; hypotheses are needed.
(= (finite-gate-mp-conclusion-event $t1 $s1 $t2 $s2 $t3 $s3 $tc $sc)
   (if (and (all3-cut-event $t1 $s1 $t2 $s2 $t3 $s3)
            (all3-imp-cut-event $t1 $s1 $t2 $s2 $t3 $s3 $tc $sc))
       (is-ge $tc $sc)
       no-mp-hypotheses))

; Positive MP case: all premises pass, implication passes, conclusion passes.
!(assertEqual
  (finite-gate-mp-conclusion-event 0.6 0.7 0.8 0.85 0.4 0.5 0.9 0.95)
  True)

; Negative guardrail #1: implication is vacuously true but the finite gate
; itself fails, so MP cannot be used to claim the failed conclusion.
!(assertEqual
  (all3-imp-cut-event 0.6 0.7 0.8 0.85 0.4 0.39 0.9 0.8)
  True)
!(assertEqual
  (finite-gate-mp-conclusion-event 0.6 0.7 0.8 0.85 0.4 0.39 0.9 0.8)
  no-mp-hypotheses)

; Negative guardrail #2: the finite gate holds but the implication fails, so MP
; still cannot be used to claim the failed conclusion.
!(assertEqual
  (all3-cut-event 0.6 0.7 0.8 0.85 0.4 0.5)
  True)
!(assertEqual
  (all3-imp-cut-event 0.6 0.7 0.8 0.85 0.4 0.5 0.9 0.8)
  False)
!(assertEqual
  (finite-gate-mp-conclusion-event 0.6 0.7 0.8 0.85 0.4 0.5 0.9 0.8)
  no-mp-hypotheses)
