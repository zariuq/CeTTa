!(reset-runtime-stats!)

(pair a b)
(pair b c)

(= (pair-path)
   (match &self
     (, (pair $x $y)
        (pair $y $z))
     (path-eq $x $y $z)))

!(assertEqualToResult
  (match &self
    (, (pair $x $y)
       (pair $y $z))
    (path-direct $x $y $z))
  ((path-direct a b c)))
!(bind! &direct-stats (runtime-stats!))
!(match &direct-stats (runtime-counter imported-bridge-v2-hit $n) $n)
!(match &direct-stats (runtime-counter imported-bridge-v3-hit $n) $n)
!(match &direct-stats (runtime-counter imported-bridge-v3-fallback $n) $n)

!(reset-runtime-stats!)
!(assertEqualToResult
  (pair-path)
  ((path-eq a b c)))
!(bind! &eq-stats (runtime-stats!))
!(match &eq-stats (runtime-counter imported-bridge-v2-hit $n) $n)
!(match &eq-stats (runtime-counter imported-bridge-v3-hit $n) $n)
!(match &eq-stats (runtime-counter imported-bridge-v3-fallback $n) $n)
