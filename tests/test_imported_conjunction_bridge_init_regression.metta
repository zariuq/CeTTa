!(reset-runtime-stats!)

(pair a b)
(pair b c)

!(match &self
  (, (pair $x $y)
     (pair $y $z))
  (path $x $y $z))
!(bind! &stats (runtime-stats!))
!(match &stats (runtime-counter imported-bridge-v2-hit $n) $n)
!(match &stats (runtime-counter imported-bridge-v3-hit $n) $n)
!(match &stats (runtime-counter imported-bridge-v3-fallback $n) $n)
