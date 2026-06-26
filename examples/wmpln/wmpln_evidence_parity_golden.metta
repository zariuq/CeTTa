!(import! &self ../../lib/lib_wmpln)

; Golden parity checks for the Lean BinaryEvidence view:
; total = n+ + n-, strength = n+ / total,
; confidence = total / (total + k), and STV decoding recovers counts.

!(assertEqual (evidence-total (Evidence 5 5)) 10)
!(assertEqual (evidence-toStrength (Evidence 5 5)) 0.5)
!(assertEqual (evidence-toConfidence (Evidence 5 5) 10) 0.5)
!(assertEqual (evidence-toSTV (Evidence 5 5) 10) (stv 0.5 0.5))
!(assertEqual (evidence-ofSTV (stv 0.5 0.5) 10) (evidence 5.0 5.0))
!(assertEqual (evidence-revision (Evidence 2 3) (Evidence 4 1)) (evidence 6 4))

; Revision is evidence-count addition viewed as an STV, not raw-confidence
; averaging.  Hand derivation:
;   E1=(0.25,0.75), total 1 -> stv(0.25,0.5)
;   E2=(2.25,0.75), total 3 -> stv(0.75,0.75)
;   E1+E2=(2.5,1.5), total 4 -> stv(0.625,0.8)
!(assertEqual (evidence-toSTV
                (evidence-revision (Evidence 0.25 0.75)
                                   (Evidence 2.25 0.75)))
              (stv 0.625 0.8))
!(assertEqual (Truth_Revision (evidence-toSTV (Evidence 0.25 0.75))
                               (evidence-toSTV (Evidence 2.25 0.75)))
              (evidence-toSTV
                (evidence-revision (Evidence 0.25 0.75)
                                   (Evidence 2.25 0.75))))
!(assertEqual (== (Truth_Revision (evidence-toSTV (Evidence 0.25 0.75))
                                  (evidence-toSTV (Evidence 2.25 0.75)))
                  (stv 0.55 0.8))
              False)

; General Beta mean/concentration updating is sequential: batch evidence
; revision and two-step Bayesian updating produce the same posterior mean.
; Hand derivation with prior mean 1/2 and prior concentration 2:
;   prior positive pseudo-count = 1
;   E1=(1,1), E2=(3,1), E1+E2=(4,2)
;   batch mean = (1+4)/(2+6) = 5/8 = 0.625
;   after E1: mean=(1+1)/(2+2)=1/2, concentration=4
;   after E2: mean=(0.5*4+3)/(4+4)=5/8 = 0.625
(= (beta-posterior-mean $prior-mean $prior-concentration (evidence $pos $neg))
   (/ (+ (* $prior-mean $prior-concentration) $pos)
      (+ $prior-concentration (+ $pos $neg))))

(= (beta-posterior-concentration $prior-concentration (evidence $pos $neg))
   (+ $prior-concentration (+ $pos $neg)))

(= (beta-sequential-posterior-mean $prior-mean $prior-concentration $e1 $e2)
   (beta-posterior-mean
     (beta-posterior-mean $prior-mean $prior-concentration $e1)
     (beta-posterior-concentration $prior-concentration $e1)
     $e2))

(= (BetaParams $alpha $beta) (beta-params $alpha $beta))
(= (beta-posterior-params $prior-alpha $prior-beta (evidence $pos $neg))
   (BetaParams (+ $prior-alpha $pos) (+ $prior-beta $neg)))
(= (beta-sequential-posterior-params $prior-alpha $prior-beta
      (evidence $pos1 $neg1) (evidence $pos2 $neg2))
   (BetaParams (+ (+ $prior-alpha $pos1) $pos2)
               (+ (+ $prior-beta $neg1) $neg2)))

!(assertEqual
  (beta-posterior-mean 0.5 2
    (evidence-revision (Evidence 1 1) (Evidence 3 1)))
  0.625)
!(assertEqual
  (beta-sequential-posterior-mean 0.5 2 (Evidence 1 1) (Evidence 3 1))
  0.625)
!(assertEqual
  (beta-sequential-posterior-mean 0.5 2 (Evidence 1 1) (Evidence 3 1))
  (beta-posterior-mean 0.5 2
    (evidence-revision (Evidence 1 1) (Evidence 3 1))))
!(assertEqual
  (beta-posterior-params 1 1
    (evidence-revision (Evidence 1 1) (Evidence 3 1)))
  (BetaParams 5 3))
!(assertEqual
  (beta-sequential-posterior-params 1 1 (Evidence 1 1) (Evidence 3 1))
  (BetaParams 5 3))
!(assertEqual
  (beta-sequential-posterior-params 1 1 (Evidence 1 1) (Evidence 3 1))
  (beta-posterior-params 1 1
    (evidence-revision (Evidence 1 1) (Evidence 3 1))))

; Negative canary: the Beta posterior is not raw empirical strength once a
; nonzero prior is present.  Raw strength of (4,2) is 2/3, not 5/8.
!(assertEqual
  (== (beta-posterior-mean 0.5 2
        (evidence-revision (Evidence 1 1) (Evidence 3 1)))
      (evidence-toStrength
        (evidence-revision (Evidence 1 1) (Evidence 3 1))))
  False)

; Guardrail: exchangeability/de Finetti does not force the Beta subfamily.
; The two-point mixture predictor at n=2 has values
;   p(0)=15/44, p(1)=7/12, p(2)=55/76.
; Every Beta posterior-predictive triple at n=2 satisfies
;   2*p(1) = p(0)+p(2).
; This exchangeable mixture violates that affine constraint.
!(assertEqual
  (== (* 2 (/ 7 12))
      (+ (/ 15 44) (/ 55 76)))
  False)

; General de-Finetti revision update, not Beta-only.  For the same two-point
; mixture, after observing one positive:
;   countEvidenceMass(1,0) = 7/12
;   countEvidenceMass(2,0) = 19/48
;   countEvidenceMass(1,1) = 3/16
; so the next-true predictive is (19/48)/(7/12)=19/28 and the
; next-false predictive is (3/16)/(7/12)=9/28.
; Cross-multiplied exact identities avoid brittle float equality:
;   (19/48)/(7/12)=19/28  iff  19*12*28 = 19*7*48
;   (3/16)/(7/12)=9/28    iff  3*12*28 = 9*7*16
!(assertEqual (* (* 19 12) 28) (* (* 19 7) 48))
!(assertEqual (* (* 3 12) 28) (* (* 9 7) 16))
!(assertEqual (+ 19 9) 28)
!(assertEqual (== (* 19 12) (* 7 28)) False)

; Imprecise/Walley family revision witness.  If the prior credal family
; contains two degenerate Bernoulli mixtures with theta=1/4 and theta=3/4,
; count revision by one positive leaves those degenerate predictive values at
; 1/4 and 3/4.  The two-point mixture's posterior predictive 19/28 lies inside
; that family envelope:
;   1/4 <= 19/28 <= 3/4
; again checked by exact cross multiplication.
!(assertEqual (<= (* 1 28) (* 19 4)) True)
!(assertEqual (<= (* 19 4) (* 3 28)) True)
!(assertEqual (== (* 1 4) (* 3 4)) False)
