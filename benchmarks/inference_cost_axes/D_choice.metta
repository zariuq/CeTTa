(= (pick $n) (if (== $n 0) a (pick (- $n 1))))
(= (pick $n) (if (== $n 0) b (pick (- $n 1))))
!(collapse (pick 17))
