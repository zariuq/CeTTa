; Arithmetic ordering rounds a rational met with a float to the nearest
; float; == retains exact value equality.  Both signs, rational rounding and
; NaNs.
!(let $_ (translatePredicate (number_string $q "1r10")) (println! (rational-positive (< $q 0.1) (> $q 0.1) (<= $q 0.1) (>= $q 0.1) (== $q 0.1) (< 0.1 $q) (<= 0.1 $q))))
!(let $_ (translatePredicate (number_string $q "-1r10")) (println! (rational-negative (< $q -0.1) (> $q -0.1) (<= $q -0.1) (>= $q -0.1) (== $q -0.1))))
!(let $_ (translatePredicate (number_string $q "7r22")) (println! (rational-third (< $q 0.3181818181818182) (> $q 0.3181818181818182) (<= $q 0.3181818181818182) (>= $q 0.3181818181818182) (== $q 0.3181818181818182))))
!(let $_ (translatePredicate (number_string $q "1r10")) (let $_ (translatePredicate (is $n nan)) (println! (nan-order (< $q $n) (<= $q $n) (> $n $q) (>= $n $q)))))
