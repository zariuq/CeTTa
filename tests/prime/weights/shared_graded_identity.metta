(=w (coin) 2 3)
(=w (coin) 2 3)
(=w (coin) 5 7)

; The identity keeps its value and contributes that value as its grade.
(= (weigh-by-self $x) $x (where $x))
(= (paired $x) (Pair $x $x))

!(weight:read (algebra 0 1 + *) (weigh-by-self (weigh-by-self 7)))

; The two uses of x share the one selected argument contribution.
!(weight:read (algebra 0 1 + *) (paired (weigh-by-self (coin))))
!(select 7 (search-policy (control FIFO (budget 2000) (workers 2)))
   (weight:read (algebra 0 1 + *) (paired (weigh-by-self (coin)))))
