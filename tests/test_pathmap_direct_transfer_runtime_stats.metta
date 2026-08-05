!(bind! &source (new-space pathmap))
!(add-atoms &source
   ((transfer duplicate) (transfer duplicate) (transfer distinct)))
!(bind! &target (new-space pathmap))

!(reset-runtime-stats!)
!(add-atoms &target (get-atoms &source))
!(bind! &stats (runtime-stats!))

!(assertEqualToResult (size &target) (3))
!(assertEqualToResult
  (size (collapse (match &target (transfer duplicate) $x)))
  (2))
!(assertEqualToResult
  (match &stats (runtime-counter pathmap-direct-transfer-call $n) $n)
  (1))
!(assertEqualToResult
  (match &stats (runtime-counter pathmap-direct-transfer-occurrence $n) $n)
  (3))
!(assertEqualToResult
  (match &stats (runtime-counter pathmap-direct-transfer-publication $n) $n)
  (1))
!(assertEqualToResult
  (match &stats (runtime-counter pathmap-projection-capture $n) $n)
  (0))
!(assertEqualToResult
  (match &stats (runtime-counter pathmap-materialize-native $n) $n)
  (0))
!(assertEqualToResult
  (match &stats (runtime-counter pathmap-direct-store $n) $n)
  (0))
