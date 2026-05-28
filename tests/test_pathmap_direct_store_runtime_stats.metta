!(reset-runtime-stats!)
!(bind! &p (new-space pathmap))
!(add-atom &p (probe a))
!(add-atom &p (probe b))
!(assertEqualToResult (size &p) (2))

!(bind! &stats (runtime-stats!))
!(assertEqualToResult
  (match &stats (runtime-counter pathmap-direct-store $n) $n)
  (2))

!(assertEqualToResult
  (match &p (probe a) hit)
  (hit))

!(reset-runtime-stats!)
!(remove-atom &p (probe a))
!(assertEqualToResult (size &p) (1))
!(assertEqualToResult
  (match &p (probe a) hit)
  ())

!(bind! &remove-stats (runtime-stats!))
!(assertEqualToResult
  (match &remove-stats (runtime-counter pathmap-direct-remove $n) $n)
  (1))
!(assertEqualToResult
  (match &remove-stats (runtime-counter space-revision-bump $n) $n)
  (1))
