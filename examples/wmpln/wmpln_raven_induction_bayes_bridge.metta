!(import! &self ../../lib/lib_wmpln)

; Runnable mirror of the RavenAsymmetricInduction -> Bayes/Inversion bridge.
;
; Dataset:
;   5 black ravens
;   95 black non-ravens
;
; Forward evidence:
;   Raven -> Black has strength 5/5 = 1.
;
; Inverse evidence:
;   Black -> Raven has strength 5/(5+95) = 1/20 = 0.05.
;
; WM-calc reading:
;   P(Raven | Black) = P(Black | Raven) * P(Raven) / P(Black)
;                    = 1 * 0.05 / 1
;                    = 0.05.

(= (abs-diff $x $y)
   (if (< $x $y) (- $y $x) (- $x $y)))

(= (near $x $y)
   (< (abs-diff $x $y) 0.0000001))

(= (forward-raven-black-strength $ravens)
   (/ $ravens $ravens))

(= (inverse-black-raven-strength $ravens $otherBlack)
   (/ $ravens (+ $ravens $otherBlack)))

(= (bayes-inversion $sAB $pA $pB)
   (/ (* $sAB $pA) $pB))

(= (raven-base-rate $ravens $otherBlack)
   (/ $ravens (+ $ravens $otherBlack)))

; Positive value witnesses: the example scores are hand-derived.
!(assertEqualToResult
   (near (forward-raven-black-strength 5.0) 1.0)
   (True))
!(assertEqualToResult
   (near (inverse-black-raven-strength 5.0 95.0) 0.05)
   (True))
!(assertEqualToResult
   (near (raven-base-rate 5.0 95.0) 0.05)
   (True))
!(assertEqualToResult
   (near (bayes-inversion 1.0 (raven-base-rate 5.0 95.0) 1.0) 0.05)
   (True))

; Positive: the inverse count-strength is the Bayes inversion of the forward
; count-strength through the observed base rate.
!(assertEqualToResult
   (near
      (inverse-black-raven-strength 5.0 95.0)
      (bayes-inversion
         (forward-raven-black-strength 5.0)
         (raven-base-rate 5.0 95.0)
         1.0))
   (True))

; Positive: the inverse link is strictly weaker than the forward link.
!(assertEqualToResult
   (< (inverse-black-raven-strength 5.0 95.0)
      (forward-raven-black-strength 5.0))
   (True))

; Negative: symmetric inversion is not valid here; the inverse is not the same
; strength as the forward link.
!(assertEqualToResult
   (near
      (inverse-black-raven-strength 5.0 95.0)
      (forward-raven-black-strength 5.0))
   (False))

; Negative: using the historical same-strength inversion would miss the base
; rate dilution.
!(assertEqualToResult
   (near (bayes-inversion 1.0 (raven-base-rate 5.0 95.0) 1.0) 1.0)
   (False))
