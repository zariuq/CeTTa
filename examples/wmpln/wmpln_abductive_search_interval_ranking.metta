!(import! &self ../../lib/lib_wmpln)

; Abductive search should rank explanations by honest credal interval
; separation, not by a point estimate alone.
;
; Positive case:
;   P(A)=P(B)=1/2, P(A->B)=2/3.
;   weak:   P(C)=1/4, P(B|C)=1/2, so P(C|B)=1/4.
;           point = 1/4, interval = [0, 1/2].
;   strong: P(C)=3/4, P(B|C)=2/3, so P(C|B)=1.
;           point = 5/6, interval = [2/3, 1].
;   Since 1/2 < 2/3, the intervals strictly rank strong over weak.
;
; Negative case:
;   P(A)=P(B)=1/2, P(A->B)=1/2.
;   open:         P(C)=1/2, P(B|C)=1/2, point = 1/2, interval = [0, 1].
;   better-point: P(C)=3/4, P(B|C)=2/3, point = 3/4, interval = [1/2, 1].
;   The point improves, but the intervals overlap; strict interval ranking
;   would be a false precision claim.

(= (stv-strength (stv $s $c)) $s)
(= (stv-confidence (stv $s $c)) $c)

(= (abs-diff $x $y)
   (if (< $x $y) (- $y $x) (- $x $y)))

(= (near $x $y)
   (< (abs-diff $x $y) 0.0000001))

(= (in-closed-interval $lo $x $hi)
   (and (<= $lo $x) (<= $x $hi)))

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

; Positive strict-rank case.
!(assertEqualToResult
   (near (abduction-point 0.6666666666666666 0.5 0.5 0.5 0.25) 0.25)
   (True))
!(assertEqualToResult
   (near (abduction-point 0.6666666666666666 0.6666666666666666 0.5 0.5 0.75)
         0.8333333333333333)
   (True))
!(assertEqualToResult
   (near (abduction-lower 0.5 0.5 0.25 0.6666666666666666 0.5) 0.0)
   (True))
!(assertEqualToResult
   (near (abduction-upper 0.5 0.5 0.25 0.6666666666666666 0.5) 0.5)
   (True))
!(assertEqualToResult
   (near (abduction-lower 0.5 0.5 0.75 0.6666666666666666 0.6666666666666666)
         0.6666666666666666)
   (True))
!(assertEqualToResult
   (near (abduction-upper 0.5 0.5 0.75 0.6666666666666666 0.6666666666666666)
         1.0)
   (True))
!(assertEqualToResult
   (strict-rank
     (abduction-lower 0.5 0.5 0.75 0.6666666666666666 0.6666666666666666)
     (abduction-upper 0.5 0.5 0.25 0.6666666666666666 0.5))
   (True))
!(assertEqualToResult
   (in-closed-interval
     (abduction-lower 0.5 0.5 0.25 0.6666666666666666 0.5)
     (abduction-point 0.6666666666666666 0.5 0.5 0.5 0.25)
     (abduction-upper 0.5 0.5 0.25 0.6666666666666666 0.5))
   (True))
!(assertEqualToResult
   (in-closed-interval
     (abduction-lower 0.5 0.5 0.75 0.6666666666666666 0.6666666666666666)
     (abduction-point 0.6666666666666666 0.6666666666666666 0.5 0.5 0.75)
     (abduction-upper 0.5 0.5 0.75 0.6666666666666666 0.6666666666666666))
   (True))
; Since the intervals are separated, any selected points inside them remain
; ranked the same way; here 0.40 is inside weak [0,1/2] and 0.70 is inside
; strong [2/3,1].
!(assertEqualToResult
   (< 0.40 0.70)
   (True))

; Negative no-rank case: the point estimate improves, but the intervals overlap.
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
   (near (abduction-lower 0.5 0.5 0.5 0.5 0.5) 0.0)
   (True))
!(assertEqualToResult
   (near (abduction-upper 0.5 0.5 0.5 0.5 0.5) 1.0)
   (True))
!(assertEqualToResult
   (near (abduction-lower 0.5 0.5 0.75 0.5 0.6666666666666666) 0.5)
   (True))
!(assertEqualToResult
   (near (abduction-upper 0.5 0.5 0.75 0.5 0.6666666666666666) 1.0)
   (True))
!(assertEqualToResult
   (overlap
     (abduction-lower 0.5 0.5 0.5 0.5 0.5)
     (abduction-upper 0.5 0.5 0.5 0.5 0.5)
     (abduction-lower 0.5 0.5 0.75 0.5 0.6666666666666666)
     (abduction-upper 0.5 0.5 0.75 0.5 0.6666666666666666))
   (True))
!(assertEqualToResult
   (strict-rank
     (abduction-lower 0.5 0.5 0.75 0.5 0.6666666666666666)
     (abduction-upper 0.5 0.5 0.5 0.5 0.5))
   (False))
!(assertEqualToResult
   (strict-rank
     (abduction-lower 0.5 0.5 0.5 0.5 0.5)
     (abduction-upper 0.5 0.5 0.75 0.5 0.6666666666666666))
   (False))
