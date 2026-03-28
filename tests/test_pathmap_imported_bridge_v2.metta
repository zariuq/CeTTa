!(reset-runtime-stats!)

(pair a b)
(pair b c)

!(assertEqualToResult
  (match &self (pair a $x) $x)
  (b))
!(bind! &unary-stats (runtime-stats!))
!(assertEqualToResult
  (match &unary-stats (runtime-counter imported-bridge-v2-hit $n) (> $n 0))
  (True))
!(assertEqualToResult
  (match &unary-stats (runtime-counter imported-bridge-v2-fallback $n) $n)
  (0))

!(reset-runtime-stats!)
!(assertEqualToResult
  (match &self
    (, (pair $x $y)
       (pair $y $z))
    (path $x $y $z))
  ((path a b c)))
!(bind! &conj-stats (runtime-stats!))
!(assertEqualToResult
  (match &conj-stats (runtime-counter imported-bridge-v2-hit $n) $n)
  (0))
!(assertEqualToResult
  (match &conj-stats (runtime-counter imported-bridge-v2-fallback $n) $n)
  (0))
