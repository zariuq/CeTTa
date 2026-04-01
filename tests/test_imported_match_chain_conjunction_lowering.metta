!(reset-runtime-stats!)

(edge a b)
(edge b c)
(edge c d)

!(assertEqualToResult
  (match &self (edge $x $y)
    (match &self (edge $y $z)
      (match &self (edge $z $w)
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
  (match &stats (runtime-counter imported-bridge-v3-hit $n) (> $n 0))
  (True))
!(assertEqualToResult
  (match &stats (runtime-counter imported-bridge-v3-fallback $n) $n)
  (0))
