!(import! &self ../../lib/lib_wmpln)

; Runnable shadow of PLN.algorithmicPriorAbduction_*_canary.
;
; Abduction supplies a point estimate and an honest credal interval.  Explanation
; selection then multiplies that interval by a hypothesis prior.  The prior can
; favor simpler hypotheses, but a robust rank is justified only when the
; prior-weighted intervals are separated.
;
; Positive case:
;   simple:  raw point 3/4, interval [1/2, 1], prior 2^-1 = 1/2
;            weighted point 3/8, weighted interval [1/4, 1/2]
;   complex: raw point 5/6, interval [2/3, 1], prior 2^-4 = 1/16
;            weighted point 5/96, weighted interval [1/24, 1/16]
;   Raw point favors complex, but prior-weighted intervals strictly rank simple.
;
; Negative case:
;   open:         raw point 1/2, interval [0, 1], prior 2^-1 = 1/2
;                 weighted point 1/4, interval [0, 1/2]
;   better-point: raw point 3/4, interval [1/2, 1], prior 2^-2 = 1/4
;                 weighted point 3/16, interval [1/8, 1/4]
;   The prior flips point scores, but intervals overlap, so no strict rank is
;   justified.

(= (stv-strength (stv $s $c)) $s)
(= (stv-confidence (stv $s $c)) $c)

(= (abs-diff $x $y)
   (if (< $x $y) (- $y $x) (- $x $y)))

(= (near $x $y)
   (< (abs-diff $x $y) 0.0000001))

(= (strict-rank $better-lo $worse-hi)
   (< $worse-hi $better-lo))

(= (overlap $lo1 $hi1 $lo2 $hi2)
   (and (<= $lo1 $hi2) (<= $lo2 $hi1)))

(= (deduction-joint-ab $pA $sAB)
   (* $pA $sAB))

(= (deduction-joint-bc $pB $sBC)
   (* $pB $sBC))

(= (deduction-b-branch-lower $pA $pB $sAB $sBC)
   (max 0.0
        (- (+ (deduction-joint-ab $pA $sAB)
              (deduction-joint-bc $pB $sBC))
           $pB)))

(= (deduction-b-branch-upper $pA $pB $sAB $sBC)
   (min (deduction-joint-ab $pA $sAB)
        (deduction-joint-bc $pB $sBC)))

(= (deduction-not-b-branch-lower $pA $pB $pC $sAB $sBC)
   (max 0.0
        (- (+ (- $pA (deduction-joint-ab $pA $sAB))
              (- $pC (deduction-joint-bc $pB $sBC)))
           (- 1.0 $pB))))

(= (deduction-not-b-branch-upper $pA $pB $pC $sAB $sBC)
   (min (- $pA (deduction-joint-ab $pA $sAB))
        (- $pC (deduction-joint-bc $pB $sBC))))

(= (deduction-credal-strength-lower $pA $pB $pC $sAB $sBC)
   (/ (+ (deduction-b-branch-lower $pA $pB $sAB $sBC)
         (deduction-not-b-branch-lower $pA $pB $pC $sAB $sBC))
      $pA))

(= (deduction-credal-strength-upper $pA $pB $pC $sAB $sBC)
   (/ (+ (deduction-b-branch-upper $pA $pB $sAB $sBC)
         (deduction-not-b-branch-upper $pA $pB $pC $sAB $sBC))
      $pA))

(= (bayes-invert $sCB $pB $pC)
   (/ (* $sCB $pC) $pB))

(= (abduction-point $sAB $sCB $pA $pB $pC)
   (stv-strength
     (Truth_Abduction (stv $pA 0.1)
                      (stv $pB 0.2)
                      (stv $pC 0.3)
                      (stv $sAB 0.4)
                      (stv $sCB 0.5))))

(= (abduction-lower $pA $pB $pC $sAB $sCB)
   (deduction-credal-strength-lower
     $pA $pB $pC $sAB (bayes-invert $sCB $pB $pC)))

(= (abduction-upper $pA $pB $pC $sAB $sCB)
   (deduction-credal-strength-upper
     $pA $pB $pC $sAB (bayes-invert $sCB $pB $pC)))

(= (prior-len1) 0.5)
(= (prior-len2) 0.25)
(= (prior-len4) 0.0625)

; Finite executable shadow of the universal-mixture `priorFromConditional`
; hook: the Lean theorem works for arbitrary nonnegative flattened
; universal-mixture priors; this witness uses the same hand-derived prior
; numbers as the description-length canary.
(= (prior-from-conditional-simple) (prior-len1))
(= (prior-from-conditional-complex) (prior-len4))

(= (prior-weighted-point $prior $point)
   (* $prior $point))

(= (prior-weighted-lower $prior $lo)
   (* $prior $lo))

(= (prior-weighted-upper $prior $hi)
   (* $prior $hi))

(= (universal-mixture-abduction-lower $prior $lo)
   (prior-weighted-lower $prior $lo))

(= (universal-mixture-abduction-upper $prior $hi)
   (prior-weighted-upper $prior $hi))

; Finite arithmetic shadow of
; PLN.xiKpfAbduction_interval_rank_of_machine_margin.  This is NOT an
; executable Solomonoff semimeasure.  It checks the theorem's margin shape:
; changing universal machines can shrink the better prior by a forward factor
; and grow the worse prior by the reciprocal reverse factor, so robust ranking
; survives only when the old-machine interval gap absorbs both constants.
(= (machine-margin-better $forward-factor $old-better-prior $better-lo)
   (* (* $forward-factor $old-better-prior) $better-lo))

(= (machine-margin-worse $old-worse-prior $reverse-factor $worse-hi)
   (* (/ $old-worse-prior $reverse-factor) $worse-hi))

(= (machine-margin-point-better $forward-factor $old-better-prior $better-point)
   (* (* $forward-factor $old-better-prior) $better-point))

(= (machine-margin-point-worse $old-worse-prior $reverse-factor $worse-point)
   (* (/ $old-worse-prior $reverse-factor) $worse-point))

; Positive: the prior turns a weaker raw point into a robust interval winner.
!(assertEqualToResult
   (near (abduction-point 0.5 0.6666666666666666 0.5 0.5 0.75) 0.75)
   (True))
!(assertEqualToResult
   (near (abduction-point 0.6666666666666666 0.6666666666666666 0.5 0.5 0.75)
         0.8333333333333333)
   (True))
!(assertEqualToResult
   (< (abduction-point 0.5 0.6666666666666666 0.5 0.5 0.75)
      (abduction-point 0.6666666666666666 0.6666666666666666 0.5 0.5 0.75))
   (True))
!(assertEqualToResult
   (near
     (prior-weighted-point (prior-len1)
       (abduction-point 0.5 0.6666666666666666 0.5 0.5 0.75))
     0.375)
   (True))
!(assertEqualToResult
   (near
     (prior-weighted-point (prior-len4)
       (abduction-point 0.6666666666666666 0.6666666666666666 0.5 0.5 0.75))
     0.05208333333333333)
   (True))
!(assertEqualToResult
   (< (prior-weighted-point (prior-len4)
        (abduction-point 0.6666666666666666 0.6666666666666666 0.5 0.5 0.75))
      (prior-weighted-point (prior-len1)
        (abduction-point 0.5 0.6666666666666666 0.5 0.5 0.75)))
   (True))
!(assertEqualToResult
   (near
     (prior-weighted-lower (prior-len1)
       (abduction-lower 0.5 0.5 0.75 0.5 0.6666666666666666))
     0.25)
   (True))
!(assertEqualToResult
   (near
     (prior-weighted-upper (prior-len1)
       (abduction-upper 0.5 0.5 0.75 0.5 0.6666666666666666))
     0.5)
   (True))
!(assertEqualToResult
   (near
     (prior-weighted-lower (prior-len4)
       (abduction-lower 0.5 0.5 0.75 0.6666666666666666 0.6666666666666666))
     0.041666666666666664)
   (True))
!(assertEqualToResult
   (near
     (prior-weighted-upper (prior-len4)
       (abduction-upper 0.5 0.5 0.75 0.6666666666666666 0.6666666666666666))
     0.0625)
   (True))
!(assertEqualToResult
   (strict-rank
     (prior-weighted-lower (prior-len1)
       (abduction-lower 0.5 0.5 0.75 0.5 0.6666666666666666))
     (prior-weighted-upper (prior-len4)
       (abduction-upper 0.5 0.5 0.75 0.6666666666666666 0.6666666666666666)))
   (True))
; Because the prior-weighted intervals are separated, any selected points
; inside them remain ranked after weighting.  Here 0.60 is inside the simple
; interval [1/2,1] and 0.90 is inside the complex interval [2/3,1]:
;   (1/16)*0.90 = 0.05625 < (1/2)*0.60 = 0.30.
!(assertEqualToResult
   (< (prior-weighted-point (prior-len4) 0.90)
      (prior-weighted-point (prior-len1) 0.60))
   (True))
; Same robust point-readout guarantee through the universal-mixture prior hook.
!(assertEqualToResult
   (strict-rank
     (universal-mixture-abduction-lower
       (prior-from-conditional-simple)
       (abduction-lower 0.5 0.5 0.75 0.5 0.6666666666666666))
     (universal-mixture-abduction-upper
       (prior-from-conditional-complex)
       (abduction-upper 0.5 0.5 0.75 0.6666666666666666 0.6666666666666666)))
   (True))
!(assertEqualToResult
   (< (prior-weighted-point (prior-from-conditional-complex) 0.90)
      (prior-weighted-point (prior-from-conditional-simple) 0.60))
   (True))
; Machine-change margin example, hand-derived:
;   old better prior = 1/2, better lower = 1/2, forward factor = 0.8
;     protected better lower = 0.8 * 1/2 * 1/2 = 0.2
;   old worse prior = 1/16, worse upper = 1, reverse factor = 0.8
;     inflated worse upper = (1/16)/0.8 = 0.078125
;   0.078125 < 0.2, so the robust rank survives this bounded change.
!(assertEqualToResult
   (near (machine-margin-better 0.8 (prior-from-conditional-simple) 0.5)
         0.2)
   (True))
!(assertEqualToResult
   (near (machine-margin-worse (prior-from-conditional-complex) 0.8 1.0)
         0.078125)
   (True))
!(assertEqualToResult
   (strict-rank
     (machine-margin-better 0.8 (prior-from-conditional-simple) 0.5)
     (machine-margin-worse (prior-from-conditional-complex) 0.8 1.0))
   (True))
; Point-readout corollary of the same margin theorem.  With selected points
; 0.60 in the better interval and 0.90 in the worse interval:
;   protected better point = 0.8 * 1/2 * 0.60 = 0.24
;   inflated worse point = (1/16)/0.8 * 0.90 = 0.0703125
!(assertEqualToResult
   (near (machine-margin-point-better 0.8 (prior-from-conditional-simple) 0.60)
         0.24)
   (True))
!(assertEqualToResult
   (near (machine-margin-point-worse (prior-from-conditional-complex) 0.8 0.90)
         0.0703125)
   (True))
!(assertEqualToResult
   (< (machine-margin-point-worse (prior-from-conditional-complex) 0.8 0.90)
      (machine-margin-point-better 0.8 (prior-from-conditional-simple) 0.60))
   (True))
; Negative margin: if both constants are too small, the distortion can swamp
; the old-machine interval gap, so no machine-invariant ranking is justified.
;   protected better lower = 0.25 * 1/2 * 1/2 = 0.0625
;   inflated worse upper = (1/16)/0.25 = 0.25
!(assertEqualToResult
   (near (machine-margin-better 0.25 (prior-from-conditional-simple) 0.5)
         0.0625)
   (True))
!(assertEqualToResult
   (near (machine-margin-worse (prior-from-conditional-complex) 0.25 1.0)
         0.25)
   (True))
!(assertEqualToResult
   (strict-rank
     (machine-margin-better 0.25 (prior-from-conditional-simple) 0.5)
     (machine-margin-worse (prior-from-conditional-complex) 0.25 1.0))
   (False))
; The selected-point readout fails for the same reason:
;   protected better point = 0.25 * 1/2 * 0.60 = 0.075
;   inflated worse point = (1/16)/0.25 * 0.90 = 0.225
!(assertEqualToResult
   (near (machine-margin-point-better 0.25 (prior-from-conditional-simple) 0.60)
         0.075)
   (True))
!(assertEqualToResult
   (near (machine-margin-point-worse (prior-from-conditional-complex) 0.25 0.90)
         0.225)
   (True))
!(assertEqualToResult
   (< (machine-margin-point-worse (prior-from-conditional-complex) 0.25 0.90)
      (machine-margin-point-better 0.25 (prior-from-conditional-simple) 0.60))
   (False))

; Negative: point ranking flips, but overlapping intervals block robust rank.
!(assertEqualToResult
   (near (abduction-point 0.5 0.5 0.5 0.5 0.5) 0.5)
   (True))
!(assertEqualToResult
   (near (abduction-point 0.5 0.6666666666666666 0.5 0.5 0.75) 0.75)
   (True))
!(assertEqualToResult
   (< (abduction-point 0.5 0.5 0.5 0.5 0.5)
      (abduction-point 0.5 0.6666666666666666 0.5 0.5 0.75))
   (True))
!(assertEqualToResult
   (near
     (prior-weighted-point (prior-len1)
       (abduction-point 0.5 0.5 0.5 0.5 0.5))
     0.25)
   (True))
!(assertEqualToResult
   (near
     (prior-weighted-point (prior-len2)
       (abduction-point 0.5 0.6666666666666666 0.5 0.5 0.75))
     0.1875)
   (True))
!(assertEqualToResult
   (< (prior-weighted-point (prior-len2)
        (abduction-point 0.5 0.6666666666666666 0.5 0.5 0.75))
      (prior-weighted-point (prior-len1)
        (abduction-point 0.5 0.5 0.5 0.5 0.5)))
   (True))
!(assertEqualToResult
   (overlap
     (prior-weighted-lower (prior-len1)
       (abduction-lower 0.5 0.5 0.5 0.5 0.5))
     (prior-weighted-upper (prior-len1)
       (abduction-upper 0.5 0.5 0.5 0.5 0.5))
     (prior-weighted-lower (prior-len2)
       (abduction-lower 0.5 0.5 0.75 0.5 0.6666666666666666))
     (prior-weighted-upper (prior-len2)
       (abduction-upper 0.5 0.5 0.75 0.5 0.6666666666666666)))
   (True))
!(assertEqualToResult
   (strict-rank
     (prior-weighted-lower (prior-len1)
       (abduction-lower 0.5 0.5 0.5 0.5 0.5))
     (prior-weighted-upper (prior-len2)
       (abduction-upper 0.5 0.5 0.75 0.5 0.6666666666666666)))
   (False))
!(assertEqualToResult
   (strict-rank
     (prior-weighted-lower (prior-len2)
       (abduction-lower 0.5 0.5 0.75 0.5 0.6666666666666666))
     (prior-weighted-upper (prior-len1)
       (abduction-upper 0.5 0.5 0.5 0.5 0.5)))
   (False))
