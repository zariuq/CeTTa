; == compares numbers by value, across representations and at any depth,
; and a NaN equals nothing.  Hyperon compares an integer rounded to a float,
; in ==, in ordering and in matching alike, so under he-compat
; 9007199254740993 equals 9007199254740992.0; the he profile compares the
; exact values, so its ordering agrees with its ==.  Hyperon also takes a
; state as equal to itself whatever it holds, where the he profile compares
; what it holds.
(big 9007199254740993)
!(== 1 1.0)
!(== 0.0 -0.0)
!(== (* 1.0e-200 1.0e-200) (* -1.0e-200 1.0e-200))
!(== 9007199254740992 9007199254740992.0)
!(== 9007199254740993 9007199254740992.0)
!(collapse (match &self (big 9007199254740992.0) found))
!(collapse (match &self (big 9007199254740993) found))
!(== (a 0.0) (a -0.0))
!(== (1 2 3) (1.0 2.0 3.0))
!(== (a 1 2) (a 1))
!(let $n (/ 0.0 0.0) (== $n $n))
!(let $n (/ 0.0 0.0) (== (a $n) (a $n)))
!(== $x $y)
!(< 9007199254740992.0 9007199254740993)
!(<= 9007199254740993 9007199254740992.0)
!(>= 9007199254740993 9007199254740992.0)
!(<= 1 1.0)
!(let $n (/ 0.0 0.0) (< $n 1))
!(let $n (/ 0.0 0.0) (let $s (new-state $n) (== $s $s)))
!(let $n (/ 0.0 0.0) (let $s (new-state $n) (== (foo $s) (foo $s))))
!(let $s (new-state 1) (== $s $s))
