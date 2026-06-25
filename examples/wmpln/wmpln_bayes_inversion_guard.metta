!(import! &self ../../lib/lib_wmpln)

; Bayes/Inversion guardrail for the WM-calc reading of PLN inversion.
;
; The historical catalog rule `Truth_inversion` keeps the implication strength
; unchanged.  The probability-correct Bayes inversion of A->B into B->A is:
;
;   P(A|B) = P(B|A) * P(A) / P(B).
;
; These coincide only under special base-rate conditions.  The rule is also
; guarded: the joint mass P(B|A)P(A) must be <= P(B), otherwise the Bayes
; inversion leaves [0,1].

(= (stv-strength (stv $s $c)) $s)
(= (stv-confidence (stv $s $c)) $c)

(= (abs-diff $x $y)
   (if (< $x $y) (- $y $x) (- $x $y)))

(= (near $x $y)
   (< (abs-diff $x $y) 0.0000001))

(= (in-closed-interval $lo $x $hi)
   (and (<= $lo $x) (<= $x $hi)))

(= (bayes-invert-ab-to-ba $sAB $pA $pB)
   (/safe (* $sAB $pA) $pB))

(= (bayes-inversion-admissible $sAB $pA $pB)
   (and (<= 0.0 $sAB)
        (and (<= $sAB 1.0)
             (and (<= 0.0 $pA)
                  (and (<= $pA 1.0)
                       (and (< 0.0 $pB)
                            (and (<= $pB 1.0)
                                 (<= (* $sAB $pA) $pB))))))))

(= (catalog-agrees-with-bayes $sAB $pA $pB)
   (near (bayes-invert-ab-to-ba $sAB $pA $pB) $sAB))

(= (catalog-agreement-condition $sAB $pA $pB)
   (or (near $sAB 0.0) (near $pA $pB)))

; Positive feasible Bayes inversion:
;   P(A)=1/4, P(B)=1/2, P(B|A)=4/5.
;   P(A|B)=(4/5)(1/4)/(1/2)=2/5.
!(assertEqualToResult
   (bayes-inversion-admissible 0.8 0.25 0.5)
   (True))
!(assertEqualToResult
   (near (bayes-invert-ab-to-ba 0.8 0.25 0.5) 0.4)
   (True))
!(assertEqualToResult
   (in-closed-interval 0.0 (bayes-invert-ab-to-ba 0.8 0.25 0.5) 1.0)
   (True))
!(assertEqualToResult
   (near (* (bayes-invert-ab-to-ba 0.8 0.25 0.5) 0.5)
         (* 0.8 0.25))
   (True))

; Equal base rates are the special case where Bayes inversion and the catalog
; same-strength shortcut agree for nonzero strength.
!(assertEqualToResult
   (near (bayes-invert-ab-to-ba 0.8 0.5 0.5) 0.8)
   (True))
!(assertEqualToResult
   (catalog-agrees-with-bayes 0.8 0.5 0.5)
   (True))
!(assertEqualToResult
   (catalog-agreement-condition 0.8 0.5 0.5)
   (True))

; The other agreement case is vacuous: zero implication strength agrees even
; when the base rates differ.
!(assertEqualToResult
   (near (bayes-invert-ab-to-ba 0.0 0.25 0.5) 0.0)
   (True))
!(assertEqualToResult
   (catalog-agrees-with-bayes 0.0 0.25 0.5)
   (True))
!(assertEqualToResult
   (catalog-agreement-condition 0.0 0.25 0.5)
   (True))

; Negative: the catalog rule preserves strength 0.8, but Bayes inversion gives
; 0.4 for the unequal-base-rate case above.
!(assertEqualToResult
   (near (stv-strength (Truth_inversion (stv 0.5 0.9) (stv 0.8 0.7))) 0.8)
   (True))
!(assertEqualToResult
   (near (stv-confidence (Truth_inversion (stv 0.5 0.9) (stv 0.8 0.7)))
         0.378)
   (True))
!(assertEqualToResult
   (near (stv-strength (Truth_inversion (stv 0.5 0.9) (stv 0.8 0.7)))
         (bayes-invert-ab-to-ba 0.8 0.25 0.5))
   (False))
!(assertEqualToResult
   (catalog-agrees-with-bayes 0.8 0.25 0.5)
   (False))
!(assertEqualToResult
   (catalog-agreement-condition 0.8 0.25 0.5)
   (False))

; Guardrail: if P(B|A)P(A) > P(B), the naive Bayes inversion is not a
; probability-valued rule output.
!(assertEqualToResult
   (bayes-inversion-admissible 0.8 0.75 0.25)
   (False))
!(assertEqualToResult
   (near (bayes-invert-ab-to-ba 0.8 0.75 0.25) 2.4)
   (True))
!(assertEqualToResult
   (< 1.0 (bayes-invert-ab-to-ba 0.8 0.75 0.25))
   (True))
