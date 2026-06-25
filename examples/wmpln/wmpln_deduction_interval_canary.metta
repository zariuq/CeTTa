!(import! &self ../../lib/lib_wmpln)

; Runnable shadow of the Lean no-independence deduction interval in
; PLNDeduction.lean / PLNDeductionITVBridge.lean.
;
; These are hand-derived Fréchet branch bounds, not engine-echo expectations.

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

(= (deduction-credal-joint-lower $pA $pB $pC $sAB $sBC)
   (+ (deduction-b-branch-lower $pA $pB $sAB $sBC)
      (deduction-not-b-branch-lower $pA $pB $pC $sAB $sBC)))

(= (deduction-credal-joint-upper $pA $pB $pC $sAB $sBC)
   (+ (deduction-b-branch-upper $pA $pB $sAB $sBC)
      (deduction-not-b-branch-upper $pA $pB $pC $sAB $sBC)))

(= (deduction-credal-strength-lower $pA $pB $pC $sAB $sBC)
   (/ (deduction-credal-joint-lower $pA $pB $pC $sAB $sBC)
      $pA))

(= (deduction-credal-strength-upper $pA $pB $pC $sAB $sBC)
   (/ (deduction-credal-joint-upper $pA $pB $pC $sAB $sBC)
      $pA))

(= (deduction-credal-strength-width $pA $pB $pC $sAB $sBC)
   (- (deduction-credal-strength-upper $pA $pB $pC $sAB $sBC)
      (deduction-credal-strength-lower $pA $pB $pC $sAB $sBC)))

; Runnable mirror of PLNHigherOrderHOLDeductionBridge.deductionAsFinite5.
; The five coordinates are P(A), P(B), P(C), P(B|A), and P(C|B).
(= (deduction-as-finite5 $pA $pB $pC $sAB $sBC)
   (simpleDeductionStrength $pA $pB $pC $sAB $sBC))

; Collapsed case:
; pA=pB=pC=1/2, sAB=1, sBC=1/2 gives AB=1/2 and BC=1/4.
; The B-branch is forced to 1/4 and the not-B branch is forced to 0, so
; P(C|A) is the point interval [1/2, 1/2].
!(assertEqual
   (deduction-credal-strength-lower 0.5 0.5 0.5 1.0 0.5)
   0.5)
!(assertEqual
   (deduction-credal-strength-upper 0.5 0.5 0.5 1.0 0.5)
   0.5)
!(assertEqual
   (deduction-credal-strength-width 0.5 0.5 0.5 1.0 0.5)
   0.0)
!(assertEqual
   (Truth_Deduction (stv 0.5 1.0) (stv 0.5 1.0) (stv 0.5 1.0)
                    (stv 1.0 1.0) (stv 0.5 1.0))
   (stv 0.5 1.0))
!(assertEqual
   (deduction-as-finite5 0.5 0.5 0.5 1.0 0.5)
   0.5)

; Open no-independence case:
; pA=pB=pC=1/2, sAB=sBC=1/2 gives AB=BC=1/4.
; Both branch Fréchet intervals are [0,1/4], so P(C|A) ranges over [0,1].
; The traditional PLN point formula returns 1/2, which is inside the interval
; but no longer stands in for the whole no-independence result.
!(assertEqual
   (deduction-credal-strength-lower 0.5 0.5 0.5 0.5 0.5)
   0.0)
!(assertEqual
   (deduction-credal-strength-upper 0.5 0.5 0.5 0.5 0.5)
   1.0)
!(assertEqual
   (deduction-credal-strength-width 0.5 0.5 0.5 0.5 0.5)
   1.0)
!(assertEqual
   (Truth_Deduction (stv 0.5 1.0) (stv 0.5 1.0) (stv 0.5 1.0)
                    (stv 0.5 1.0) (stv 0.5 1.0))
   (stv 0.5 1.0))
!(assertEqual
   (deduction-as-finite5 0.5 0.5 0.5 0.5 0.5)
   0.5)

; Asymmetric no-independence case:
; pA=pB=1/2, pC=3/4, sAB=sBC=1/2 gives AB=BC=1/4.
; The B-branch can vary over [0,1/4], but the not-B branch is forced to 1/4,
; so P(C|A) ranges over [1/2,1].  The traditional PLN point formula returns
; 3/4, again a member of the interval rather than a replacement for it.
!(assertEqual
   (deduction-credal-strength-lower 0.5 0.5 0.75 0.5 0.5)
   0.5)
!(assertEqual
   (deduction-credal-strength-upper 0.5 0.5 0.75 0.5 0.5)
   1.0)
!(assertEqual
   (deduction-credal-strength-width 0.5 0.5 0.75 0.5 0.5)
   0.5)
!(assertEqual
   (Truth_Deduction (stv 0.5 1.0) (stv 0.5 1.0) (stv 0.75 1.0)
                    (stv 0.5 1.0) (stv 0.5 1.0))
   (stv 0.75 1.0))
!(assertEqual
   (deduction-as-finite5 0.5 0.5 0.75 0.5 0.5)
   0.75)

; Guard case: with P(A)=0, the conditional-probability consistency check must
; fail rather than divide by zero.
!(assertEqual
   (conditional-probability-consistency 0.0 0.5 0.5)
   False)
