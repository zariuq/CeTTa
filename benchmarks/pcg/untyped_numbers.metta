(= (pcg-count $n $acc)
   (if (== $n 0) $acc (pcg-count (- $n 1) (+ $acc 1))))
!(pcg-count 100000 0)
