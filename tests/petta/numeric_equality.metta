; == compares values and never binds: numbers by exact value at any depth,
; every other term as a term.  != is its negation, and ordering compares
; numbers exactly too, so it agrees with ==.  A NaN equals nothing and is
; unordered.  Terms that share subterms compare in time linear in the
; sharing, not in their unfolding.
(= (from-above) (* 1.0e-200 1.0e-200))
(= (from-below) (* -1.0e-200 1.0e-200))
(= (dag $n $leaf) (if (== $n 0) $leaf (let $t (dag (- $n 1) $leaf) (node $t $t))))
!(println! (zeros (== (from-above) (from-below)) (== 0.0 -0.0) (!= 0.0 -0.0)))
!(println! (kinds (== 1 1.0) (!= 1 1.0) (== 2.5 2.5) (== 1 2) (== "1" 1) (== a a)))
!(println! (large (== 9007199254740992 9007199254740992.0) (== 9007199254740993 9007199254740992.0) (== 100000000000000000000 1.0e20) (== 100000000000000000001 1.0e20)))
!(println! (nested (== (a 0.0) (a -0.0)) (== (1 2 3) (1.0 2.0 3.0)) (== (a 1) (a 2)) (== (a 1) (b 1)) (== (a 1 2) (a 1))))
!(println! (variables (== $x $y) (let $x 1 (== $y $y)) (let $r (== $z 1) (let $z 2 unbound))))
!(println! (nan (let $_ (translatePredicate (is $n nan)) (== $n $n)) (let $_ (translatePredicate (is $n nan)) (== (a $n) (a $n))) (let $_ (translatePredicate (is $n nan)) (!= $n $n)) (let $_ (translatePredicate (is $n nan)) (< $n 1)) (let $_ (translatePredicate (is $n nan)) (>= $n 1))))
!(println! (order (< 9007199254740992.0 9007199254740993) (> 9007199254740993 9007199254740992.0) (<= 9007199254740993 9007199254740992.0) (>= 9007199254740993 9007199254740992.0) (<= 1 1.0) (< -0.0 0.0) (< 2 3) (>= 2.5 3)))
!(println! (rationals (let $_ (translatePredicate (number_string $half "1r2")) ($half (== $half 0.5) (!= $half 0.5) (== (x $half) (x 0.5)) (< $half 0.75))) (let $_ (translatePredicate (number_string $q "7r22")) ($q (== $q 0.3181818181818182) (* $q 22)))))
!(println! (shared (== (dag 40 1) (dag 40 1.0)) (== (dag 40 1) (dag 40 2)) (let $_ (translatePredicate (is $n nan)) (let $d (dag 40 $n) (== $d $d)))))
!(let $flag (translatePredicate (set_prolog_flag float_overflow infinity)) infinity-enabled)
!(println! (infinity (== inf inf) (== (+ inf 0) (+ inf 0)) (== inf (+ inf 0))))
