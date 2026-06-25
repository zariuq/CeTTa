!(import! &self ../../lib/lib_wmpln)

; Subjective Logic is a coordinate view of the EvidenceBeta / Revision core,
; not a second PLN truth-value semantics.  These canaries mirror
; Mettapedia.Logic.SubjectiveLogicBridge.

(= (absdiff $x $y) (if (< $x $y) (- $y $x) (- $x $y)))
(= (approx $x $y) (< (absdiff $x $y) 0.000001))
(= (stv-strength (stv $s $c)) $s)
(= (stv-confidence (stv $s $c)) $c)

; Binomial Subjective Logic evidence view with base rate a.
(= (sl-den $r $s) (+ $r (+ $s 2.0)))
(= (sl-belief $r $s) (/ $r (sl-den $r $s)))
(= (sl-disbelief $r $s) (/ $s (sl-den $r $s)))
(= (sl-uncertainty $r $s) (/ 2.0 (sl-den $r $s)))
(= (sl-projected $r $s $a)
   (+ (sl-belief $r $s) (* $a (sl-uncertainty $r $s))))

; General base-rate / prior-weight view:
; prior positive mass = a*W, negative mass = (1-a)*W.
(= (sl-weighted-den $r $s $W) (+ $r (+ $s $W)))
(= (sl-weighted-belief $r $s $W) (/ $r (sl-weighted-den $r $s $W)))
(= (sl-weighted-disbelief $r $s $W) (/ $s (sl-weighted-den $r $s $W)))
(= (sl-weighted-uncertainty $r $s $W) (/ $W (sl-weighted-den $r $s $W)))
(= (sl-weighted-projected $r $s $a $W)
   (+ (sl-weighted-belief $r $s $W)
      (* $a (sl-weighted-uncertainty $r $s $W))))
(= (asymmetric-beta-mean $r $s $a $W)
   (/ (+ $r (* $a $W)) (sl-weighted-den $r $s $W)))
(= (avg2 $x $y) (/ (+ $x $y) 2.0))

; Raw evidence fusion: add sufficient statistics first, then project.
(= (sl-raw-fusion-projected $r1 $s1 $r2 $s2 $a $W)
   (sl-weighted-projected (+ $r1 $r2) (+ $s1 $s2) $a $W))

; Laplace/Beta posterior readout for symmetric prior (1,1).
(= (laplace-beta-mean $r $s) (/ (+ $r 1.0) (sl-den $r $s)))

; Empirical-count STV readout used by Truth_Revision's count-weighted merge.
(= (empirical-stv $r $s)
   (stv (/safe $r (+ $r $s)) (Truth_w2c (+ $r $s))))

; Prior-loaded projected-probability STV. This is a readout, not the raw object
; to revise if a single shared prior should be counted only once.
(= (laplace-stv $r $s)
   (stv (laplace-beta-mean $r $s) (Truth_w2c (+ $r $s))))

; Positive: SL projection equals the Beta/Laplace posterior mean.
; Hand: r=3,s=1 -> b=1/2, d=1/6, u=1/3, a=1/2, projection=2/3.
!(assertEqual (approx (sl-projected 3.0 1.0 0.5) (laplace-beta-mean 3.0 1.0)) True)
!(assertEqual (approx (sl-projected 3.0 1.0 0.5) 0.6666666666666666) True)
!(assertEqual
  (approx (+ (sl-belief 3.0 1.0)
             (+ (sl-disbelief 3.0 1.0) (sl-uncertainty 3.0 1.0)))
          1.0)
  True)

; Positive: arbitrary base-rate Subjective Logic projection is the asymmetric
; Beta posterior mean. Hand: r=2,s=1,a=1/4,W=4 -> (2 + 1)/(2+1+4)=3/7.
!(assertEqual
  (approx (sl-weighted-projected 2.0 1.0 0.25 4.0)
          (asymmetric-beta-mean 2.0 1.0 0.25 4.0))
  True)
!(assertEqual
  (approx (sl-weighted-projected 2.0 1.0 0.25 4.0) 0.42857142857142855)
  True)
!(assertEqual
  (approx (+ (sl-weighted-belief 2.0 1.0 4.0)
             (+ (sl-weighted-disbelief 2.0 1.0 4.0)
                (sl-weighted-uncertainty 2.0 1.0 4.0)))
          1.0)
  True)

; Boundary base rates: a=0 and a=1 keep the same evidence/prior weight but move
; the prior mass to the negative or positive side. Hand values: 2/7 and 6/7.
!(assertEqual
  (approx (sl-weighted-projected 2.0 1.0 0.0 4.0) 0.2857142857142857)
  True)
!(assertEqual
  (approx (sl-weighted-projected 2.0 1.0 1.0 4.0) 0.8571428571428571)
  True)

; Positive: raw SL/Beta fusion is shared-prior evidence addition before
; projection. Hand: (2,1)+(4,2), a=1/4, W=4 gives (6 + 1)/(6+3+4)=7/13.
!(assertEqual
  (approx (sl-raw-fusion-projected 2.0 1.0 4.0 2.0 0.25 4.0)
          0.5384615384615384)
  True)

; Negative: fusing already-projected prior-loaded values by averaging is a
; different operation, not the shared-prior raw-evidence update.
!(assertEqual
  (approx
    (avg2 (sl-weighted-projected 2.0 1.0 0.25 4.0)
          (sl-weighted-projected 4.0 2.0 0.25 4.0))
    (sl-raw-fusion-projected 2.0 1.0 4.0 2.0 0.25 4.0))
  False)

; Positive: empirical-count STVs revise by raw evidence-count addition.
; (6 positive,0 negative) revised with (0 positive,2 negative) -> empirical
; 6/8 = 0.75, confidence weight 8 -> c = 8/9.
!(assertEqual
  (approx
    (stv-strength (Truth_Revision (empirical-stv 6.0 0.0) (empirical-stv 0.0 2.0)))
    0.75)
  True)
!(assertEqual
  (approx
    (stv-confidence (Truth_Revision (empirical-stv 6.0 0.0) (empirical-stv 0.0 2.0)))
    (Truth_w2c 8.0))
  True)

; Negative guardrail: revising two already-prior-loaded posterior readouts is
; not the same thing as one shared-prior Beta update over combined evidence.
; Truth_Revision(laplace projections) gives 23/32 = 0.71875; the shared-prior
; combined Beta readout gives (6+1)/(6+2+2)=7/10.
!(assertEqual
  (approx
    (stv-strength (Truth_Revision (laplace-stv 6.0 0.0) (laplace-stv 0.0 2.0)))
    0.71875)
  True)
!(assertEqual (approx (laplace-beta-mean 6.0 2.0) 0.7) True)
!(assertEqual
  (approx
    (stv-strength (Truth_Revision (laplace-stv 6.0 0.0) (laplace-stv 0.0 2.0)))
    (laplace-beta-mean 6.0 2.0))
  False)
