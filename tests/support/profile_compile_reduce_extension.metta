(= (uses-reduce)
   (reduce (superpose (1 2))
           0
           $acc
           $item
           (eval (+ $acc $item))))
