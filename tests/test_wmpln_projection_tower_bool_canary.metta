!(import! &self ../lib/lib_wmpln)

; Runtime canary for the Lean credal projection tower.
;
; The carrier is intentionally tiny: two singleton Bool credal sources and the
; truth-indicator gamble.  This mirrors the Lean theorem
; `concreteBool_same_confidence_different_credal_envelope` without claiming a
; full credal-set engine in CeTTa.

(= (singleton-bool-lower singleton-false truth-indicator) 0.0)
(= (singleton-bool-lower singleton-true truth-indicator) 1.0)

(= (singleton-bool-upper singleton-false truth-indicator) 0.0)
(= (singleton-bool-upper singleton-true truth-indicator) 1.0)

(= (coordinate-confidence pln-odds $w) (Truth_w2c $w))
(= (coordinate-confidence reserve-half $w) (/ $w (+ (* 2.0 $w) 1.0)))

(= (coordinate-decode pln-odds $c) (Truth_c2w $c))
(= (coordinate-decode reserve-half $c) (/ $c (- 1.0 (* 2.0 $c))))

(= (projection-tower $credal truth-indicator $coord $weight)
   (tower $credal
          truth-indicator
          $coord
          $weight
          (itv (singleton-bool-lower $credal truth-indicator)
               (singleton-bool-upper $credal truth-indicator)
               (coordinate-confidence $coord $weight))))

(= (tower-lower (tower $credal $gamble $coord $weight (itv $l $u $c))) $l)

(= (tower-upper (tower $credal $gamble $coord $weight (itv $l $u $c))) $u)

(= (tower-width (tower $credal $gamble $coord $weight (itv $l $u $c))) (- $u $l))

(= (tower-credibility (tower $credal $gamble $coord $weight (itv $l $u $c))) $c)

(= (tower-stored-weight (tower $credal $gamble $coord $weight $itv)) $weight)

(= (tower-decoded-weight (tower $credal $gamble $coord $weight (itv $l $u $c)))
   (coordinate-decode $coord $c))

; Same coordinate and weight, different credal source: confidence stays fixed,
; but the lower envelope changes from 0 to 1.
!(assertEqual
  (tower-lower (projection-tower singleton-false truth-indicator pln-odds 1.0))
  0.0)
!(assertEqual
  (tower-lower (projection-tower singleton-true truth-indicator pln-odds 1.0))
  1.0)
!(assertEqual
  (tower-credibility (projection-tower singleton-false truth-indicator pln-odds 1.0))
  0.5)
!(assertEqual
  (tower-credibility (projection-tower singleton-true truth-indicator pln-odds 1.0))
  0.5)
!(assertEqual
  (== (tower-lower (projection-tower singleton-false truth-indicator pln-odds 1.0))
      (tower-lower (projection-tower singleton-true truth-indicator pln-odds 1.0)))
  False)
!(assertEqual
  (== (tower-credibility (projection-tower singleton-false truth-indicator pln-odds 1.0))
      (tower-credibility (projection-tower singleton-true truth-indicator pln-odds 1.0)))
  True)
!(assertEqual
  (tower-decoded-weight (projection-tower singleton-false truth-indicator pln-odds 1.0))
  1.0)
!(assertEqual
  (tower-decoded-weight (projection-tower singleton-true truth-indicator pln-odds 1.0))
  1.0)

; Same credal source and evidence weight, different coordinate: the envelope is
; unchanged, but the displayed confidence changes.
!(assertEqual
  (tower-lower (projection-tower singleton-true truth-indicator reserve-half 1.0))
  1.0)
!(assertEqual
  (tower-credibility (projection-tower singleton-true truth-indicator reserve-half 1.0))
  0.3333333333333333)
!(assertEqual
  (== (tower-credibility (projection-tower singleton-true truth-indicator pln-odds 1.0))
      (tower-credibility (projection-tower singleton-true truth-indicator reserve-half 1.0)))
  False)
!(assertEqual
  (== (tower-stored-weight (projection-tower singleton-true truth-indicator pln-odds 1.0))
      (tower-stored-weight (projection-tower singleton-true truth-indicator reserve-half 1.0)))
  True)
; Reserve-half decoding is exact in the Lean real-valued theorem and displayed
; here through finite runtime floats.
!(assertEqual
  (tower-decoded-weight (projection-tower singleton-true truth-indicator reserve-half 1.0))
  0.9999999999999999)

; Width-complement is not generic: a point envelope has width 0, while the
; selected PLN-odds confidence for weight 1 is 0.5.
!(assertEqual
  (+ (tower-width (projection-tower singleton-true truth-indicator pln-odds 1.0))
     (tower-credibility (projection-tower singleton-true truth-indicator pln-odds 1.0)))
  0.5)
!(assertEqual
  (== (+ (tower-width (projection-tower singleton-true truth-indicator pln-odds 1.0))
         (tower-credibility (projection-tower singleton-true truth-indicator pln-odds 1.0)))
      1.0)
  False)
