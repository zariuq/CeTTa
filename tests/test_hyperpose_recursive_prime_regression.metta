; Threaded hyperpose must handle pure recursive user definitions, not only
; grounded arithmetic leaves. Positive examples: the same recursive prime
; frontier succeeds under 2 and 4 worker budgets. Boundary example: the
; sequential budget must agree with the threaded budgets.

!(pragma! max-stack-depth 200000)

(= (has-divisor $n $d)
   (if (> (* $d $d) $n)
       False
       (if (== (% $n $d) 0)
           True
           (has-divisor $n (+ $d 1)))))

(= (is-prime $n)
   (if (<= $n 1)
       False
       (if (== $n 2)
           True
           (if (== (% $n 2) 0)
               False
               (not (has-divisor $n 3))))))

!(pragma! num-threads 1)
!(assertEqual
  (collapse (hyperpose ((is-prime 10007) (is-prime 10009))))
  (True True))

!(pragma! num-threads 2)
!(assertEqual
  (collapse (hyperpose ((is-prime 10007) (is-prime 10009))))
  (True True))

!(pragma! num-threads 4)
!(assertEqual
  (collapse (hyperpose ((is-prime 10007) (is-prime 10009))))
  (True True))
