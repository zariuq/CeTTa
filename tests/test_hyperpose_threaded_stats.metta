; Hyperpose must preserve its observable bag and cancellation behavior under a
; multi-worker configuration.  Runtime receipts are constrained only by
; scheduler-independent conservation laws.

!(import! &self fs)
!(import! &self system)

(= (burn $n)
   (if (== $n 0)
       False
       (burn (- $n 1))))

!(reset-runtime-stats!)
!(pragma! num-threads 2)
!(assertEqual
  (collapse (hyperpose ((fs:exists "tests/support/fs_fixture.txt") collapse-d)))
  (True collapse-d))
!(assertEqual
  (collapse (hyperpose ((get-type (system:cwd)) system-ok)))
  (String system-ok))
!(assertEqual
  (collapse (hyperpose ((superpose (collapse-a collapse-b)) collapse-c)))
  (collapse-a collapse-b collapse-c))
!(assertEqual
  (once (hyperpose ((burn 2000000) threaded-ok)))
  threaded-ok)

!(bind! &hyperpose-stats (runtime-stats!))
!(assertEqualToResult
  (match &hyperpose-stats
    (runtime-counter parallel-persistent-episode-attempt $attempt)
    (match &hyperpose-stats
      (runtime-counter parallel-persistent-episode-commit $commit)
      (match &hyperpose-stats
        (runtime-counter parallel-persistent-busy-decline $busy)
        (match &hyperpose-stats
          (runtime-counter parallel-persistent-capacity-decline $capacity)
          (== $attempt (+ $commit (+ $busy $capacity)))))))
  (True))
!(assertEqualToResult
  (match &hyperpose-stats
    (runtime-counter parallel-persistent-episode-commit $commit)
    (match &hyperpose-stats
      (runtime-counter parallel-persistent-episode-complete $complete)
      (== $commit $complete)))
  (True))
