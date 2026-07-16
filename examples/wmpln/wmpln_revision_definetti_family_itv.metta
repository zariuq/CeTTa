!(import! &self ../../lib/lib_wmpln)

; Runnable witness for the finite-source Revision -> de-Finetti posterior-family
; ITV bridge.
;
; This is a hand-derived arithmetic shadow of the Lean theorem
; revisionMany_countPairEvidence_posteriorFamilyWidthComplementITV_readout.
; It does not prove tightness or Beta-universality.  It checks that a finite
; Revision count ledger can feed a non-singleton posterior family whose lower,
; upper, width, credibility, and midpoint are preserved by the ITV packaging.

(= (abs-diff $x $y)
   (if (< $x $y) (- $y $x) (- $x $y)))

(= (near $x $y)
   (< (abs-diff $x $y) 0.000001))

(= (beta-predict-true $alpha $beta $k $l)
   (/ (+ $alpha $k) (+ $alpha (+ $beta (+ $k $l)))))

(= (beta-predict-false $alpha $beta $k $l)
   (/ (+ $beta $l) (+ $alpha (+ $beta (+ $k $l)))))

(= (interval-width $lo $hi)
   (- $hi $lo))

(= (width-complement $lo $hi)
   (- 1.0 (interval-width $lo $hi)))

(= (interval-midpoint $lo $hi)
   (/ (+ $lo $hi) 2.0))

(= (posterior-family-itv $lo $hi)
   (itv $lo $hi (width-complement $lo $hi)))

(= (itv-lower (itv $lo $hi $credibility))
   $lo)

(= (itv-upper (itv $lo $hi $credibility))
   $hi)

(= (itv-width (itv $lo $hi $credibility))
   (interval-width $lo $hi))

(= (itv-credibility (itv $lo $hi $credibility))
   $credibility)

(= (itv-strength (itv $lo $hi $credibility))
   (interval-midpoint $lo $hi))

; Non-singleton posterior family.
;
; Prior member A: Beta(1,1).  Prior member B: Beta(4,2).
; Finite Revision ledger: k=2 true observations, l=1 false observation.
;
; True predictive ratios:
;   A: (1+2)/(1+1+2+1) = 3/5 = 0.6
;   B: (4+2)/(4+2+2+1) = 6/9 = 2/3
; Envelope: [0.6, 2/3], width=1/15, credibility=14/15, midpoint=19/30.
(= (two-prior-true-lower)
   (beta-predict-true 1.0 1.0 2.0 1.0))

(= (two-prior-true-upper)
   (beta-predict-true 4.0 2.0 2.0 1.0))

(= (two-prior-true-itv)
   (posterior-family-itv (two-prior-true-lower) (two-prior-true-upper)))

; False predictive ratios:
;   A: (1+1)/(1+1+2+1) = 2/5 = 0.4
;   B: (2+1)/(4+2+2+1) = 3/9 = 1/3
; Envelope: [1/3, 0.4], width=1/15, credibility=14/15, midpoint=11/30.
(= (two-prior-false-lower)
   (beta-predict-false 4.0 2.0 2.0 1.0))

(= (two-prior-false-upper)
   (beta-predict-false 1.0 1.0 2.0 1.0))

(= (two-prior-false-itv)
   (posterior-family-itv (two-prior-false-lower) (two-prior-false-upper)))

; Singleton control family: two identical Beta(1,1) prior members collapse to a
; point after the same finite Revision ledger.
(= (singleton-true-itv)
   (posterior-family-itv
     (beta-predict-true 1.0 1.0 2.0 1.0)
     (beta-predict-true 1.0 1.0 2.0 1.0)))

; More evidence with the same 2:1 empirical ratio narrows the two-prior family:
;   Beta(1,1), k=20,l=10 -> 21/32 = 0.65625
;   Beta(4,2), k=20,l=10 -> 24/36 = 2/3
(= (two-prior-more-evidence-true-itv)
   (posterior-family-itv
     (beta-predict-true 1.0 1.0 20.0 10.0)
     (beta-predict-true 4.0 2.0 20.0 10.0)))

; Positive readouts: the ITV projections preserve the hand-derived posterior
; family envelope.
!(assertEqualToResult (near (itv-lower (two-prior-true-itv)) 0.6) (True))
!(assertEqualToResult (near (itv-upper (two-prior-true-itv)) 0.6666666666666666) (True))
!(assertEqualToResult (near (itv-width (two-prior-true-itv)) 0.0666666666666666) (True))
!(assertEqualToResult (near (itv-credibility (two-prior-true-itv)) 0.9333333333333333) (True))
!(assertEqualToResult (near (itv-strength (two-prior-true-itv)) 0.6333333333333333) (True))

!(assertEqualToResult (near (itv-lower (two-prior-false-itv)) 0.3333333333333333) (True))
!(assertEqualToResult (near (itv-upper (two-prior-false-itv)) 0.4) (True))
!(assertEqualToResult (near (itv-width (two-prior-false-itv)) 0.0666666666666666) (True))
!(assertEqualToResult (near (itv-credibility (two-prior-false-itv)) 0.9333333333333333) (True))
!(assertEqualToResult (near (itv-strength (two-prior-false-itv)) 0.36666666666666664) (True))

; Width-complement coherence: the credibility readout is exactly 1 - width.
!(assertEqualToResult
   (near (+ (itv-width (two-prior-true-itv))
            (itv-credibility (two-prior-true-itv)))
         1.0)
   (True))

; Negative guardrails: this family is not a singleton, and its midpoint is not
; either endpoint.
!(assertEqualToResult (near (itv-lower (two-prior-true-itv))
                            (itv-upper (two-prior-true-itv)))
                      (False))
!(assertEqualToResult (near (itv-width (two-prior-true-itv)) 0.0) (False))
!(assertEqualToResult (near (itv-strength (two-prior-true-itv))
                            (itv-lower (two-prior-true-itv)))
                      (False))
!(assertEqualToResult (near (itv-strength (two-prior-true-itv))
                            (itv-upper (two-prior-true-itv)))
                      (False))

; Singleton control: identical priors make the posterior family width collapse.
!(assertEqualToResult (near (itv-width (singleton-true-itv)) 0.0) (True))
!(assertEqualToResult (near (itv-credibility (singleton-true-itv)) 1.0) (True))
!(assertEqualToResult
   (< (itv-width (singleton-true-itv))
      (itv-width (two-prior-true-itv)))
   (True))

; More finite evidence narrows the family while preserving the same endpoint
; semantics.
!(assertEqualToResult (near (itv-lower (two-prior-more-evidence-true-itv)) 0.65625) (True))
!(assertEqualToResult (near (itv-upper (two-prior-more-evidence-true-itv)) 0.6666666666666666) (True))
!(assertEqualToResult
   (< (itv-width (two-prior-more-evidence-true-itv))
      (itv-width (two-prior-true-itv)))
   (True))
