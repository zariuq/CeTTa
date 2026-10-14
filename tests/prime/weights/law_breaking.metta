!(pragma! prefer-rationals True)

; A coefficient above one under multiplication.  The sprinkler derivation
; weighs 1/10 until, 600 steps later, a factor of 20 lifts it to 2, the
; best.  Stopping once no pending derivation's coefficient so far beats
; rain (27/50) would return rain and miss it.
(= (slow $n $v) (if (== $n 0) $v (slow (- $n 1) $v)))
(=w (explain wet) (Via sprinkler (explain sprinkler)) 1/10)
(=w (explain wet) (Via rain (explain rain)) 9/10)
(=w (explain rain) (slow 300 (Seen clouds)) 6/10)
(= (explain sprinkler) (slow 600 (timer)))
(=w (timer) (Seen timer) 20)

!(select 1 (search-policy (control WO (order by-weight descending)
     (law unit-interval-product) (budget 10000000)))
   (weight:read (algebra 0 1 + *) (explain wet)))

; The same law declared for an algebra it does not fit.
!(select 1 (search-policy (control WO (order by-weight descending)
     (law nonnegative-sum) (budget 10000000)))
   (weight:read (algebra 0 1 + *) (explain wet)))
