!(bind! &data (new-space pathmap))
!(add-atoms &data ((edge a b) (edge b c) (edge c d)))
!(reset-runtime-stats!)

!(assertEqualToResult
  (match &data (edge $x $y)
    (match &data (edge $y $z)
      (match &data (edge $z $w)
        (path $x $y $z $w))))
  ((path a b c d)))
!(bind! &stats (runtime-stats!))
!(assertEqualToResult
  (match &stats (runtime-counter imported-bridge-v2-hit $n) $n)
  (0))
!(assertEqualToResult
  (match &stats (runtime-counter imported-bridge-v2-fallback $n) $n)
  (0))
!(assertEqualToResult
  (match &stats (runtime-counter imported-bridge-v3-hit $n) $n)
  (0))
!(assertEqualToResult
  (match &stats (runtime-counter imported-bridge-v3-fallback $n) $n)
  (0))
!(assertEqualToResult
  (match &stats (runtime-counter pathmap-indexed-query $n) $n)
  (1))
