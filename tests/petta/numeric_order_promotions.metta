; Arithmetic ordering promotes mixed float operands; == retains exact value
; equality. Exercise both signs, integer sizes, rational rounding and NaNs.
!(println! (integers (< -9007199254740993 -9007199254740992.0) (>= -9007199254740993 -9007199254740992.0) (> 9223372036854775807 9.223372036854776e18) (<= 9223372036854775807 9.223372036854776e18) (> 100000000000000000001 1.0e20) (<= 100000000000000000001 1.0e20) (== 100000000000000000001 1.0e20) (> 100000000000000000001 100000000000000000000)))
!(let $_ (translatePredicate (number_string $q "1r10")) (println! (rational-positive (< $q 0.1) (> $q 0.1) (<= $q 0.1) (>= $q 0.1) (== $q 0.1) (< 0.1 $q) (<= 0.1 $q))))
!(let $_ (translatePredicate (number_string $q "-1r10")) (println! (rational-negative (< $q -0.1) (> $q -0.1) (<= $q -0.1) (>= $q -0.1) (== $q -0.1))))
!(let $_ (translatePredicate (number_string $q "7r22")) (println! (rational-third (< $q 0.3181818181818182) (> $q 0.3181818181818182) (<= $q 0.3181818181818182) (>= $q 0.3181818181818182) (== $q 0.3181818181818182))))
!(let $_ (translatePredicate (number_string $q "1r10")) (let $_ (translatePredicate (is $n nan)) (println! (nan-order (< $q $n) (<= $q $n) (> $n $q) (>= $n $q)))))
