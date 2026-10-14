; Import native_weight_algebras.metta and probability/finite_bayes.metta.
; The caller selects exact rational arithmetic; floating leaves stay unresolved.
; Exact symbolic real expressions with explicit rational enclosure readout.
; Log leaves have positive rational arguments. Refinement changes precision,
; not the expression; overlapping intervals do not decide comparison.

(= (weights:real)
   (algebra (RealQ 0) (RealQ 1) real:add real:mul))
(= (real:add $a $b) (RealPlus $a $b))
(= (real:mul $a $b) (RealTimes $a $b))
(= (real:valid? (RealQ $q)) (weight:exact-rational? $q))
(= (real:valid? (RealLog $q))
   (and (weight:exact-rational? $q) (> $q 0)))
(= (real:valid? (RealPlus $a $b)) (and (real:valid? $a) (real:valid? $b)))
(= (real:valid? (RealTimes $a $b)) (and (real:valid? $a) (real:valid? $b)))
(= (real:min $a $b) (if (< $a $b) $a $b))
(= (real:max $a $b) (if (< $a $b) $b $a))
(= (real:interval-plus (bayes:interval $a $b) (bayes:interval $c $d))
   (bayes:interval (+ $a $c) (+ $b $d)))
(= (real:interval-times (bayes:interval $a $b) (bayes:interval $c $d))
   (let* (($ac (* $a $c)) ($ad (* $a $d))
          ($bc (* $b $c)) ($bd (* $b $d)))
     (bayes:interval (real:min (real:min $ac $ad) (real:min $bc $bd))
                     (real:max (real:max $ac $ad) (real:max $bc $bd)))))
(= (real:enclose (RealQ $q) $precision)
   (if (weight:exact-rational? $q) (bayes:interval $q $q)
     (RealUnresolved nonrational-leaf (RealQ $q))))
(= (real:enclose (RealPlus $a $b) $precision)
   (real:interval-plus (real:enclose $a $precision) (real:enclose $b $precision)))
(= (real:enclose (RealTimes $a $b) $precision)
   (real:interval-times (real:enclose $a $precision) (real:enclose $b $precision)))
(= (real:enclose (RealLog $q) $precision)
   (if (and (weight:exact-rational? $q)
            (and (weight:exact-rational? $precision) (weights:natural? $precision)))
     (bayes:log-interval $q $precision)
     (RealUnresolved invalid-log-input (RealLog $q) $precision)))
(= (real:compare-intervals $left $right $precision
      (bayes:interval $ll $lu) (bayes:interval $rl $ru))
   (if (and (and (weight:exact-rational? $ll) (weight:exact-rational? $lu))
            (and (weight:exact-rational? $rl) (weight:exact-rational? $ru)))
    (if (< $lu $rl) Less
     (if (< $ru $ll) Greater
       (if (and (and (== $ll $lu) (== $rl $ru)) (== $ll $rl)) Equal
         (RealUnresolved $left $right $precision
           (bayes:interval $ll $lu) (bayes:interval $rl $ru)))))
    (RealUnresolved nonrational-enclosure $left $right $precision)))
(= (real:compare $left $right $precision)
   (if (and (real:valid? $left) (real:valid? $right))
     (real:compare-intervals $left $right $precision
       (real:enclose $left $precision) (real:enclose $right $precision))
     (RealInvalidInput $left $right $precision)))
(= (real:refine (RealUnresolved $left $right $precision $li $ri) $more)
   (if (weights:natural? $more) (real:compare $left $right (+ $precision $more))
     (RealUnresolved invalid-increment $more)))
