!(import! &self ../../lib/lib_wmpln)

; Runnable shadow of OpenCog Classic's attraction-profile intensional rules:
;   opencog/pln/rules/intensional/intensional-inheritance-direct-introduction.scm
;   opencog/pln/rules/intensional/intensional-similarity-direct-introduction.scm
;
; This is an implementation/regression witness, not a MeTTapedia Lean ontology.
; It pins what the old rule computed and exposes the confidence-handling weak
; spot as a concrete CeTTa example.

(= (absdiff $x $y) (if (< $x $y) (- $y $x) (- $x $y)))
(= (approx $x $y) (< (absdiff $x $y) 0.000001))

(= (sum3 $x $y $z) (+ $x (+ $y $z)))
(= (fuzzy-intersection3 $a1 $a2 $a3 $b1 $b2 $b3)
   (+ (min $a1 $b1)
      (+ (min $a2 $b2)
         (min $a3 $b3))))
(= (fuzzy-union3 $a1 $a2 $a3 $b1 $b2 $b3)
   (+ (max $a1 $b1)
      (+ (max $a2 $b2)
         (max $a3 $b3))))

; Classic IntensionalInheritance(A,B):
;   Sum_X min(pattern(X,A), pattern(X,B)) / Sum_X pattern(X,B)
; with the OpenCog convention that A inherits B means A specializes B, so the
; denominator is the B-profile. Empty evidence returns strength 1 but has zero
; confidence below.
(= (classic-intensional-inheritance3 $a1 $a2 $a3 $b1 $b2 $b3)
   (let* (($den (sum3 $b1 $b2 $b3)))
     (if (> $den 0.0)
         (/ (fuzzy-intersection3 $a1 $a2 $a3 $b1 $b2 $b3) $den)
         1.0)))

; Classic IntensionalSimilarity(A,B):
;   Sum_X min(pattern(X,A), pattern(X,B)) / Sum_X max(pattern(X,A), pattern(X,B))
(= (classic-intensional-similarity3 $a1 $a2 $a3 $b1 $b2 $b3)
   (let* (($den (fuzzy-union3 $a1 $a2 $a3 $b1 $b2 $b3)))
     (if (> $den 0.0)
         (/ (fuzzy-intersection3 $a1 $a2 $a3 $b1 $b2 $b3) $den)
         1.0)))

; OpenCog Classic confidence for these direct-introduction rules is the count of
; B attraction links converted to confidence.
(= (classic-attraction-confidence $count) (Truth_w2c $count))

; The similarity rule already uses mean*confidence for A's pattern strength,
; while the inheritance rule contains a TODO asking whether it should. This
; confidence-weighted variant is the cleaner profile readout: weak evidence
; should not dominate the pattern geometry.
(= (pattern-strength-weighted $mean $confidence) (* $mean $confidence))
(= (weighted-intensional-similarity3
      $a1m $a1c $a2m $a2c $a3m $a3c
      $b1m $b1c $b2m $b2c $b3m $b3c)
   (classic-intensional-similarity3
     (pattern-strength-weighted $a1m $a1c)
     (pattern-strength-weighted $a2m $a2c)
     (pattern-strength-weighted $a3m $a3c)
     (pattern-strength-weighted $b1m $b1c)
     (pattern-strength-weighted $b2m $b2c)
     (pattern-strength-weighted $b3m $b3c)))

; Positive: faithful classic arithmetic.
; A=(.9,.2,0), B=(.6,.2,.4)
; intersection=.8, B-sum=1.2, union=1.5.
!(assertEqual
  (approx (classic-intensional-inheritance3 0.9 0.2 0.0 0.6 0.2 0.4)
          0.6666666666666666)
  True)
!(assertEqual
  (approx (classic-intensional-similarity3 0.9 0.2 0.0 0.6 0.2 0.4)
          0.5333333333333333)
  True)
!(assertEqual (approx (classic-attraction-confidence 3.0) 0.75) True)

; Negative / structural: inheritance is directional, similarity is symmetric.
!(assertEqual
  (approx (classic-intensional-inheritance3 0.6 0.2 0.4 0.9 0.2 0.0)
          0.7272727272727273)
  True)
!(assertEqual
  (approx
    (classic-intensional-inheritance3 0.9 0.2 0.0 0.6 0.2 0.4)
    (classic-intensional-inheritance3 0.6 0.2 0.4 0.9 0.2 0.0))
  False)
!(assertEqual
  (approx
    (classic-intensional-similarity3 0.9 0.2 0.0 0.6 0.2 0.4)
    (classic-intensional-similarity3 0.6 0.2 0.4 0.9 0.2 0.0))
  True)

; Empty-profile edge: the old strength convention is vacuously 1, but its
; evidence count yields confidence 0. A modern consumer must carry both fields.
!(assertEqual
  (approx (classic-intensional-inheritance3 0.0 0.0 0.0 0.0 0.0 0.0) 1.0)
  True)
!(assertEqual
  (approx (classic-intensional-similarity3 0.0 0.0 0.0 0.0 0.0 0.0) 1.0)
  True)
!(assertEqual (approx (classic-attraction-confidence 0.0) 0.0) True)

; Grounding canary: the mean-only classic profile lets a low-confidence shared
; pattern dominate similarity. Confidence-weighted profiles shrink it.
; Means: A=(.9,.4,0), B=(.9,0,0). Classic similarity = .9/1.3.
; Confidences: shared .9-pattern has c=.1, so weighted similarity = .09/.49.
!(assertEqual
  (approx (classic-intensional-similarity3 0.9 0.4 0.0 0.9 0.0 0.0)
          0.6923076923076923)
  True)
!(assertEqual
  (approx
    (weighted-intensional-similarity3
      0.9 0.1 0.4 1.0 0.0 1.0
      0.9 0.1 0.0 1.0 0.0 1.0)
    0.1836734693877551)
  True)
!(assertEqual
  (< (weighted-intensional-similarity3
       0.9 0.1 0.4 1.0 0.0 1.0
       0.9 0.1 0.0 1.0 0.0 1.0)
     (classic-intensional-similarity3 0.9 0.4 0.0 0.9 0.0 0.0))
  True)

; Same means, different confidence: the classic profile is unchanged, while the
; weighted profile registers that the shared pattern is now well-supported.
!(assertEqual
  (approx
    (classic-intensional-similarity3 0.9 0.4 0.0 0.9 0.0 0.0)
    (classic-intensional-similarity3 0.9 0.4 0.0 0.9 0.0 0.0))
  True)
!(assertEqual
  (< (weighted-intensional-similarity3
       0.9 0.1 0.4 1.0 0.0 1.0
       0.9 0.1 0.0 1.0 0.0 1.0)
     (weighted-intensional-similarity3
       0.9 1.0 0.4 1.0 0.0 1.0
       0.9 1.0 0.0 1.0 0.0 1.0))
  True)
