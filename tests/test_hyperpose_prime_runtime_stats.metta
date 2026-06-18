; Runtime-stats witness for the real threaded recursive matcher/evaluator path.
; This stays small enough for the stats build, but still exercises recursive
; user equations rather than the synthetic control drafts.

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

!(reset-runtime-stats!)
!(pragma! num-threads 2)
!(assertEqual
  (collapse (hyperpose ((is-prime 10000019) (is-prime 10000079))))
  (True True))

!(bind! &stats (runtime-stats!))
!(assertEqualToResult
  (match &stats
    (runtime-counter hyperpose-threaded-run $n)
    (> $n 0))
  (True))
!(assertEqualToResult
  (match &stats
    (runtime-counter parallel-worker-task $n)
    (> $n 0))
  (True))
!(assertEqualToResult
  (match &stats
    (runtime-counter hyperpose-cooperative-fallback $n)
    (== $n 0))
  (True))
!(assertEqualToResult
  (match &stats
    (runtime-counter arena-spare-miss $n)
    (> $n 0))
  (True))
!(assertEqualToResult
  (match &stats
    (runtime-counter eval-arena-reserved-bytes-peak $n)
    (> $n 0))
  (True))
!(assertEqualToResult
  (match &stats
    (runtime-counter scratch-arena-reserved-bytes-peak $n)
    (> $n 0))
  (True))
!(assertEqualToResult
  (match &stats
    (runtime-counter bindings-entry-pool-bytes-peak $n)
    (> $n 0))
  (True))
