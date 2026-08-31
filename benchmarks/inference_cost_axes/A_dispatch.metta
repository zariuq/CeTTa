(= (count $n) (if (== $n 0) done (count (- $n 1))))
(= (count $n) (if (< $n 0) never (empty)))
!(count 300000)
