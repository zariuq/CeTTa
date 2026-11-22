; == between SWI's rationals and floats compares exact values, and with
; SWI's float_overflow flag set to infinity, inf is a value that equals
; itself.
!(println! (rationals (let $_ (translatePredicate (number_string $half "1r2")) ($half (== $half 0.5) (!= $half 0.5) (== (x $half) (x 0.5)) (< $half 0.75))) (let $_ (translatePredicate (number_string $q "7r22")) ($q (== $q 0.3181818181818182) (* $q 22)))))
!(let $flag (translatePredicate (set_prolog_flag float_overflow infinity)) infinity-enabled)
!(println! (infinity (== inf inf) (== (+ inf 0) (+ inf 0)) (== inf (+ inf 0))))
