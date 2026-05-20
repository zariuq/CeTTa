; Runtime-stats lane witness: hyperpose with more than one configured worker
; uses the pthread-backed scheduler for eligible static branch lists.

(= (burn $n)
   (if (== $n 0)
       False
       (burn (- $n 1))))

!(reset-runtime-stats!)
!(pragma! hyperpose-threads 2)
!(assertEqual
  (collapse (hyperpose ((superpose (collapse-a collapse-b)) collapse-c)))
  (collapse-a collapse-b collapse-c))
!(assertEqual
  (once (hyperpose ((burn 2000000) threaded-ok)))
  threaded-ok)

!(bind! &hyperpose-stats (runtime-stats!))
!(assertEqualToResult
  (match &hyperpose-stats
    (runtime-counter hyperpose-threaded-run $n)
    (> $n 0))
  (True))
!(assertEqualToResult
  (match &hyperpose-stats
    (runtime-counter hyperpose-worker-started $n)
    (> $n 0))
  (True))
!(assertEqualToResult
  (match &hyperpose-stats
    (runtime-counter hyperpose-cancel-request $n)
    (> $n 0))
  (True))
