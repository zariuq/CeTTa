!(import! &self ../../lib/lib_wmpln)

; Independence-point approximation canaries.
;
; These witnesses distinguish three things that are easy to conflate:
;   1. the cheap product-coupling / independence point;
;   2. the no-independence Frechet interval around that point;
;   3. representation artifacts such as averaging Beta-component counts.
;
; Expected values are hand-derived in the comments and mirror
; Mettapedia.Logic.PLNIndependencePointApproximation.

(= (abs-diff $x $y)
   (if (< $x $y) (- $y $x) (- $x $y)))

(= (near $x $y)
   (< (abs-diff $x $y) 0.0000001))

; EXP1: under the product coupling, the full two-by-two pushforward mean for
; f(x,y)=x*y equals the product of marginal means.
; X={0.2,0.8}, Y={0.3,0.9}, both half/half.
; Full mean = .25*(.06+.18+.24+.72)=.30; marginal product = .5*.6=.30.
(= (two-point-product-pushforward-mean $x0 $x1 $y0 $y1 $wx $wy)
   (+ (* (* $wx $wy) (* $x0 $y0))
      (+ (* (* $wx (- 1.0 $wy)) (* $x0 $y1))
         (+ (* (* (- 1.0 $wx) $wy) (* $x1 $y0))
            (* (* (- 1.0 $wx) (- 1.0 $wy)) (* $x1 $y1))))))

(= (two-point-marginal-mean $x0 $x1 $w)
   (+ (* $w $x0) (* (- 1.0 $w) $x1)))

!(assertEqualToResult
   (near
      (two-point-product-pushforward-mean 0.2 0.8 0.3 0.9 0.5 0.5)
      0.3)
   (True))
!(assertEqualToResult
   (near
      (two-point-product-pushforward-mean 0.2 0.8 0.3 0.9 0.5 0.5)
      (* (two-point-marginal-mean 0.2 0.8 0.5)
         (two-point-marginal-mean 0.3 0.9 0.5)))
   (True))

; EXP2: the independence point is one interior coupling, not the whole answer.
; Couplings preserving half/half marginals are:
;   [[t, .5-t], [.5-t, t]], t in [0,.5].
; E[f](t)=.36*t+.21, so Frechet interval [0.21,0.39] and independence
; point t=.25 gives 0.30.
(= (two-point-halfhalf-coupling-product-mean $t)
   (+ (* $t 0.06)
      (+ (* (- 0.5 $t) 0.18)
         (+ (* (- 0.5 $t) 0.24)
            (* $t 0.72)))))

!(assertEqualToResult (near (two-point-halfhalf-coupling-product-mean 0.0) 0.21) (True))
!(assertEqualToResult (near (two-point-halfhalf-coupling-product-mean 0.25) 0.30) (True))
!(assertEqualToResult (near (two-point-halfhalf-coupling-product-mean 0.5) 0.39) (True))
!(assertEqualToResult (< 0.21 0.30) (True))
!(assertEqualToResult (< 0.30 0.39) (True))

; EXP3: global count is a concentration projection of the whole law, not a
; weighted average of arbitrary component counts.
; For mean .5 and variance .25, kappa=.5*.5/.25-1=0.
(= (effective-concentration $mean $var)
   (- (/ (* $mean (- 1.0 $mean)) $var) 1.0))

(= (weighted-average-count $w1 $c1 $w2 $c2)
   (+ (* $w1 $c1) (* $w2 $c2)))

!(assertEqualToResult (near (effective-concentration 0.5 0.25) 0.0) (True))
!(assertEqualToResult
   (near (weighted-average-count 0.5 1000.0 0.5 1000.0) 1000.0)
   (True))
!(assertEqualToResult
   (near (effective-concentration 0.5 0.25)
         (weighted-average-count 0.5 1000.0 0.5 1000.0))
   (False))

; EXP4: the real WM-PLN deduction point formula is useful, but can hide the
; entire no-independence interval.  With all visible strengths/base rates .5,
; simpleDeductionStrength gives .5 while the credal strength interval is [0,1].
(= (deduction-joint-ab $pA $sAB) (* $pA $sAB))
(= (deduction-joint-bc $pB $sBC) (* $pB $sBC))
(= (deduction-b-branch-lower $pA $pB $sAB $sBC)
   (max 0.0 (- (+ (deduction-joint-ab $pA $sAB)
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
   (/ (deduction-credal-joint-lower $pA $pB $pC $sAB $sBC) $pA))
(= (deduction-credal-strength-upper $pA $pB $pC $sAB $sBC)
   (/ (deduction-credal-joint-upper $pA $pB $pC $sAB $sBC) $pA))
(= (deduction-credal-strength-width $pA $pB $pC $sAB $sBC)
   (- (deduction-credal-strength-upper $pA $pB $pC $sAB $sBC)
      (deduction-credal-strength-lower $pA $pB $pC $sAB $sBC)))

!(assertEqualToResult
   (near (simpleDeductionStrength 0.5 0.5 0.5 0.5 0.5) 0.5)
   (True))
!(assertEqualToResult
   (near (deduction-credal-strength-lower 0.5 0.5 0.5 0.5 0.5) 0.0)
   (True))
!(assertEqualToResult
   (near (deduction-credal-strength-upper 0.5 0.5 0.5 0.5 0.5) 1.0)
   (True))
!(assertEqualToResult
   (near (deduction-credal-strength-width 0.5 0.5 0.5 0.5 0.5) 1.0)
   (True))

; EXP5: algebraic structure can make the cheap point exact.  If sAB=1, the
; branch allocation is forced and the no-independence width collapses to zero.
!(assertEqualToResult
   (near (deduction-credal-strength-width 0.5 0.5 0.5 1.0 0.5) 0.0)
   (True))
!(assertEqualToResult
   (near (simpleDeductionStrength 0.5 0.5 0.5 1.0 0.5)
         (deduction-credal-strength-lower 0.5 0.5 0.5 1.0 0.5))
   (True))
!(assertEqualToResult
   (< (deduction-credal-strength-width 0.5 0.5 0.5 0.9 0.5)
      (deduction-credal-strength-width 0.5 0.5 0.5 0.5 0.5))
   (True))
