; A finite fuel grant is one affine purse.  Until parallel budget splitting is
; explicitly qualified, hyperpose must preserve that purse by using the
; cooperative evaluator instead of copying it into worker threads.

!(reset-runtime-stats!)
!(pragma! num-threads 2)
!(pragma! max-stack-depth 100)
!(assertEqual
  (once (hyperpose (bounded-left bounded-right)))
  bounded-left)

!(bind! &stats (runtime-stats!))
!(assertEqualToResult
  (match &stats
    (runtime-counter hyperpose-threaded-run $n)
    (== $n 0))
  (True))
!(assertEqualToResult
  (match &stats
    (runtime-counter hyperpose-cooperative-fallback $n)
    (> $n 0))
  (True))
