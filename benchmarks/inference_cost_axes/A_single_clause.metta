(= (count $n) (if (== $n 0) done (count (- $n 1))))
!(count 300000)
