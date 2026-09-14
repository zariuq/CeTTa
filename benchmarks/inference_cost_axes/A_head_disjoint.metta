(= (count 0) done)
(= (count $n) (if (> $n 0) (count (- $n 1)) (empty)))
!(count 300000)
