; Scheduler-invariance witness for recursive matcher/evaluator work.  This
; stays small enough for the stats build while exercising user equations rather
; than a synthetic control draft.

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
  (collapse (hyperpose ((is-prime 10000019) (is-prime 10000079))))
  (True True))
!(pragma! num-threads 2)
!(assertEqual
  (collapse (hyperpose ((is-prime 10000019) (is-prime 10000079))))
  (True True))
