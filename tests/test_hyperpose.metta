; hyperpose is same-frontier nondeterminism with a separate scheduling policy.
; Positive examples: pure branches collect like superpose and nested fan-out is
; preserved. Negative/boundary examples: zero or one configured thread keeps the
; sequential source-order fallback, while branches that depend on named shared
; handles still stay on the sequential path.

!(assertEqualToResult (hyperpose (a b Empty c)) (a b c))
!(assertEqual (collapse (hyperpose (a Empty b))) (a b))
!(assertEqual (collapse (hyperpose ((superpose (x y)) z))) (x y z))
!(assertEqual (collapse (hyperpose (same same))) (same same))

!(pragma! num-threads 0)
!(assertEqual (select 2 (hyperpose (a b c))) (a b))
!(pragma! num-threads 1)
!(assertEqual (once (hyperpose (left right))) left)

!(bind! &hyperpose-effects (new-space))
!(pragma! num-threads 2)
!(collapse
  (hyperpose
    ((add-atom &hyperpose-effects (seen a))
     (add-atom &hyperpose-effects (seen b)))))
!(assertEqual
  (collapse (match &hyperpose-effects (seen $x) $x))
  (a b))
