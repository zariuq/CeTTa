; The runtime-stats extension exposes named nonnegative counters and reset
; returns every counter to zero.  Optimizer route and allocation values are
; observations for benchmarks, not golden language results.

!(reset-runtime-stats!)
!(bind! &zero (runtime-stats!))
!(assertEqualToResult
  (match &zero (runtime-counter space-revision-bump $n) $n)
  (0))

!(add-atom &self (= (stats-contract-probe A) ok))
!(assertEqualToResult (stats-contract-probe A) (ok))
!(bind! &observed (runtime-stats!))
!(assertEqualToResult
  (match &observed (runtime-counter space-revision-bump $n) (> $n 0))
  (True))

!(reset-runtime-stats!)
!(bind! &reset (runtime-stats!))
!(assertEqualToResult
  (match &reset (runtime-counter space-revision-bump $n) $n)
  (0))
