!(import! &self ../../lib/lib_wmpln)

; Runnable mirror of the RavenAbduction -> priorWeightedPoint bridge.
;
; Raven:
;   prior = 0.02, P(black|raven)=0.99, P(croaks|raven)=0.90
; Crow:
;   prior = 0.10, P(black|crow)=0.95, P(croaks|crow)=0.01
;
; Black alone is common and weakly discriminating:
;   raven score = 0.02*0.99 = 0.0198
;   crow score  = 0.10*0.95 = 0.095
; so the common species wins.
;
; Black + croaks is discriminating:
;   raven score = 0.02*0.99*0.90 = 0.01782
;   crow score  = 0.10*0.95*0.01 = 0.00095
; so the best explanation flips to raven.

(= (abs-diff $x $y)
   (if (< $x $y) (- $y $x) (- $x $y)))

(= (near $x $y)
   (< (abs-diff $x $y) 0.0000001))

(= (prior-weighted-point $prior $point)
   (* $prior $point))

(= (score-black $prior $likeBlack)
   (prior-weighted-point $prior $likeBlack))

(= (score-black-croak $prior $likeBlack $likeCroak)
   (prior-weighted-point $prior (* $likeBlack $likeCroak)))

; Positive value witnesses: the example scores are hand-derived.
!(assertEqualToResult
   (near (score-black 0.02 0.99) 0.0198)
   (True))
!(assertEqualToResult
   (near (score-black 0.10 0.95) 0.095)
   (True))
!(assertEqualToResult
   (near (score-black-croak 0.02 0.99 0.90) 0.01782)
   (True))
!(assertEqualToResult
   (near (score-black-croak 0.10 0.95 0.01) 0.00095)
   (True))

; Positive: base rate dominates the common feature.
!(assertEqualToResult
   (< (score-black 0.02 0.99)
      (score-black 0.10 0.95))
   (True))

; Positive: the discriminating feature flips the best explanation.
!(assertEqualToResult
   (< (score-black-croak 0.10 0.95 0.01)
      (score-black-croak 0.02 0.99 0.90))
   (True))

; Negative: black alone is not enough to rank raven over crow.
!(assertEqualToResult
   (< (score-black 0.10 0.95)
      (score-black 0.02 0.99))
   (False))

; Negative: after adding croak, crow no longer wins.
!(assertEqualToResult
   (< (score-black-croak 0.02 0.99 0.90)
      (score-black-croak 0.10 0.95 0.01))
   (False))
