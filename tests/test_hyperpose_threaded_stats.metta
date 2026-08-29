; Runtime-stats lane witness: hyperpose with more than one configured worker
; uses the pthread-backed scheduler for eligible static branch lists.

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
!(bind! &hyperpose-fs-stats (runtime-stats!))
!(assertEqualToResult
  (match &hyperpose-fs-stats
    (runtime-counter hyperpose-threaded-run $n)
    (> $n 0))
  (True))
!(assertEqualToResult
  (match &hyperpose-fs-stats
    (runtime-counter hyperpose-cooperative-fallback $n)
    (== $n 0))
  (True))
!(reset-runtime-stats!)
!(assertEqual
  (collapse (hyperpose ((get-type (system:cwd)) system-ok)))
  (String system-ok))
!(bind! &hyperpose-system-stats (runtime-stats!))
!(assertEqualToResult
  (match &hyperpose-system-stats
    (runtime-counter hyperpose-threaded-run $n)
    (> $n 0))
  (True))
!(assertEqualToResult
  (match &hyperpose-system-stats
    (runtime-counter hyperpose-cooperative-fallback $n)
    (== $n 0))
  (True))
!(reset-runtime-stats!)
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
    (runtime-counter parallel-queue-push $n)
    (> $n 0))
  (True))
!(assertEqualToResult
  (match &hyperpose-stats
    (runtime-counter parallel-worker-task $n)
    (> $n 0))
  (True))
!(assertEqualToResult
  (match &hyperpose-stats
    (runtime-counter hyperpose-cancel-request $n)
    (> $n 0))
  (True))
!(assertEqualToResult
  (match &hyperpose-stats
    (runtime-counter hyperpose-cancel-observed $n)
    (> $n 0))
  (True))
