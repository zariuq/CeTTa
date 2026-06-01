; Expression heads must not be speculatively evaluated while probing for
; dynamic callable captures. The tuple evaluator may evaluate each element
; once, but capture discovery must not fire side effects before that pass.

!(bind! &tuple-head-effects (new-space))
(= (record-tuple-head $x)
   (add-atom &tuple-head-effects $x))

!(let $_ (((record-tuple-head A)))
   (assertEqual
     (collapse (match &tuple-head-effects A A))
     (A)))

!(bind! &tuple-sequence-effects (new-space))
(= (record-tuple-seq $x)
   (add-atom &tuple-sequence-effects $x))

!(let $_ (((record-tuple-seq M) (record-tuple-seq W)))
   (assertEqual
     (collapse (match &tuple-sequence-effects $x $x))
     (M W)))
