; Numbers compare by exact value across representations, and every relation
; agrees: ==, ordering, matching, equation heads, unique and numeric-eq.
; % pairs with // as floor division, as in Python, and a NaN equals nothing
; and is unordered.

; Rationals and bigints against floats
!(== 1/2 0.5)
!(== 1/3 0.3333333333333333)
!(> 1/3 0.3333333333333333)
!(<= 1/2 0.5)
!(== 18446744073709551616 18446744073709551616.0)
!(numeric-eq 1/2 0.5)
(baz 1/2)
(qux 0.5)
(big 18446744073709551616)
!(collapse (match &self (baz 0.5) found))
!(collapse (match &self (qux 1/2) found))
!(collapse (match &self (big 18446744073709551616.0) found))
(= (half 1/2) rational-head)
!(half 0.5)
!(unique (superpose (1/2 0.5 1 1.0)))
!(case 0.5 ((1/2 rational) ($_ other)))

; Floor division and its remainder, a == (a // b) * b + a % b
!(% -7 3)
!(// -7 3)
!(+ (* 3 (// -7 3)) (% -7 3))
!(% 7 -3)
!(% -9223372036854775809 10)
!(% -7/2 3)
!(// 7/2 3)
!(// 1 0.1)
!(% 1 0.1)
!(% -7.5 2)
!(// -7.5 2)
!(% 1.0 0.0)

; A NaN equals nothing, even itself, and is unordered
!(let $n (/ 0.0 0.0) (== $n $n))
!(let $n (/ 0.0 0.0) (=alpha $n $n))
!(let $n (/ 0.0 0.0) (unique (superpose ($n $n))))
!(let $n (/ 0.0 0.0) (< $n 1))

; A state cell compares by what it holds, so one holding a NaN is not equal
; even to itself, however the NaN got there
!(let $n (/ 0.0 0.0) (let $s (new-state $n) (== $s $s)))
!(let* (($s (new-state 0)) ($n (/ 0.0 0.0)) ($u (change-state! $s $n))) (== $s $s))
!(let $n (/ 0.0 0.0) (let $s (new-state $n) (== (foo $s) (foo $s))))
!(let $s (new-state 1) (== $s $s))

; Shared subterms compare in time linear in the sharing, not the unfolding
(= (dag $n $leaf) (if (== $n 0) $leaf (let $t (dag (- $n 1) $leaf) (node $t $t))))
!(== (dag 40 1) (dag 40 1.0))
!(== (dag 40 1) (dag 40 2))
!(let $n (/ 0.0 0.0) (== (pair (dag 40 1) $n) (pair (dag 40 1.0) $n)))
!(let $s (new-state 1) (== (dag 8 $s) (dag 8 $s)))

; A float literal past the float range reads as the nearest float
!(get-metatype 1.0e-310)
!(== 1.0e-310 (* 1.0e-200 1.0e-110))
!(== 1.0e-400 0.0)
!(== 1.0e400 (* 1.0e308 10.0))

; State cells that hold themselves compare by their unfolding
!(let* (($s (new-state 0)) ($t (new-state 0)) ($u (change-state! $s (a $s))) ($v (change-state! $t (a $t)))) (== $s $t))
!(let* (($s (new-state 0)) ($t (new-state 0)) ($u (change-state! $s (a $s))) ($v (change-state! $t (b $t)))) (== $s $t))

; prefer-rationals makes inexact division exact; equality is unchanged
!(pragma! prefer-rationals True)
!(/ 7 22)
!(== (/ 1 2) 0.5)
