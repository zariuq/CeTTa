!(import! &self ../../lib/lib_wmpln)

; Runnable shadow of PLNDerivation.plnInductionAbduction_asymmetric_canary.
;
; Hand calculation:
;   P(A)=P(C)=1/2, P(B)=3/4, premise strengths 1/2 and 1/2.
;
; Induction/source rule:
;   P(B|A) = P(A|B) P(B) / P(A) = (1/2)(3/4)/(1/2) = 3/4
;   P(C|A) = (3/4)(1/2)
;          + (1-3/4)(1/2 - (3/4)(1/2))/(1-3/4)
;          = 1/2.
;
; Abduction/sink rule:
;   P(C|B) = P(B|C) P(C) / P(B) = (1/2)(1/2)/(3/4) = 1/3
;   P(C|A) = (1/2)(1/3)
;          + (1-1/2)(1/2 - (3/4)(1/3))/(1-3/4)
;          = 2/3.
;
; No-independence credal intervals:
;   induction skeleton:  P(B|A)=3/4, P(C|B)=1/2, interval [0,1].
;   abduction skeleton:  P(B|A)=1/2, P(C|B)=1/3, interval [1/2,1].
;
; The same visible numbers therefore produce different conclusions depending
; on whether the shared node is a source or a sink.  This is the topology
; sensitivity hidden by uniform-prior examples, and the interval version records
; the corresponding difference in honest credal uncertainty.

(= (asym-induction)
   (Truth_Induction (stv 0.5 0.1)
                    (stv 0.75 0.2)
                    (stv 0.5 0.3)
                    (stv 0.5 0.5)
                    (stv 0.5 0.75)))

(= (asym-abduction)
   (Truth_Abduction (stv 0.5 0.1)
                    (stv 0.75 0.2)
                    (stv 0.5 0.3)
                    (stv 0.5 0.5)
                    (stv 0.5 0.75)))

(= (stv-strength (stv $s $c)) $s)
(= (stv-confidence (stv $s $c)) $c)

(= (abs-diff $x $y)
   (if (< $x $y) (- $y $x) (- $x $y)))

(= (near $x $y)
   (< (abs-diff $x $y) 0.0000001))

(= (in-closed-interval $lo $x $hi)
   (and (<= $lo $x) (<= $x $hi)))

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

(= (deduction-credal-strength-width $pA $pB $pC $sAB $sBC)
   (- (deduction-credal-strength-upper $pA $pB $pC $sAB $sBC)
      (deduction-credal-strength-lower $pA $pB $pC $sAB $sBC)))

(= (bayes-invert $sBA $pA $pB)
   (/ (* $sBA $pB) $pA))

(= (induction-interval-sAB)
   (bayes-invert 0.5 0.5 0.75))

(= (abduction-interval-sBC)
   (bayes-invert 0.5 0.75 0.5))

(= (catalog-agrees-with-bayes $s $denomBase $numBase)
   (near (bayes-invert $s $denomBase $numBase) $s))

!(assertEqualToResult (near (stv-strength (asym-induction)) 0.5) (True))
!(assertEqualToResult (near (stv-confidence (asym-induction)) 0.5) (True))
!(assertEqualToResult (near (stv-strength (asym-abduction)) 0.6666666666666666) (True))
!(assertEqualToResult (near (stv-confidence (asym-abduction)) 0.5) (True))
!(assertEqualToResult (< (stv-strength (asym-induction))
                         (stv-strength (asym-abduction)))
                      (True))
!(assertEqualToResult (in-closed-interval
                        (deduction-credal-strength-lower
                          0.5 0.75 0.5 (induction-interval-sAB) 0.5)
                        (stv-strength (asym-induction))
                        (deduction-credal-strength-upper
                          0.5 0.75 0.5 (induction-interval-sAB) 0.5))
                      (True))
!(assertEqualToResult (in-closed-interval
                        (deduction-credal-strength-lower
                          0.5 0.75 0.5 0.5 (abduction-interval-sBC))
                        (stv-strength (asym-abduction))
                        (deduction-credal-strength-upper
                          0.5 0.75 0.5 0.5 (abduction-interval-sBC)))
                      (True))
!(assertEqualToResult (near (deduction-credal-strength-lower
                              0.5 0.75 0.5 (induction-interval-sAB) 0.5)
                             0.0)
                      (True))
!(assertEqualToResult (near (deduction-credal-strength-upper
                              0.5 0.75 0.5 (induction-interval-sAB) 0.5)
                             1.0)
                      (True))
!(assertEqualToResult (near (deduction-credal-strength-width
                              0.5 0.75 0.5 (induction-interval-sAB) 0.5)
                             1.0)
                      (True))
!(assertEqualToResult (near (deduction-credal-strength-lower
                              0.5 0.75 0.5 0.5 (abduction-interval-sBC))
                             0.5)
                      (True))
!(assertEqualToResult (near (deduction-credal-strength-upper
                              0.5 0.75 0.5 0.5 (abduction-interval-sBC))
                             1.0)
                      (True))
!(assertEqualToResult (near (deduction-credal-strength-width
                              0.5 0.75 0.5 0.5 (abduction-interval-sBC))
                             0.5)
                      (True))
; Catalog shortcut boundary for the Bayes-inverted source-rule premise:
; it agrees at equal base rates, disagrees for the asymmetric canary, and
; agrees vacuously at zero strength.
!(assertEqualToResult (catalog-agrees-with-bayes 0.5 0.5 0.5) (True))
!(assertEqualToResult (catalog-agrees-with-bayes 0.5 0.5 0.75) (False))
!(assertEqualToResult (catalog-agrees-with-bayes 0.0 0.5 0.75) (True))

; Same boundary for the sink-rule abduction premise.
!(assertEqualToResult (catalog-agrees-with-bayes 0.5 0.75 0.75) (True))
!(assertEqualToResult (catalog-agrees-with-bayes 0.5 0.75 0.5) (False))
!(assertEqualToResult (catalog-agrees-with-bayes 0.0 0.75 0.5) (True))
!(assertEqualToResult (< (deduction-credal-strength-lower
                           0.5 0.75 0.5 (induction-interval-sAB) 0.5)
                         (deduction-credal-strength-lower
                           0.5 0.75 0.5 0.5 (abduction-interval-sBC)))
                      (True))
