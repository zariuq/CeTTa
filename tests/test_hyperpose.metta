; hyperpose is same-frontier nondeterminism with a separate scheduling policy.
; Positive examples: pure branches collect like superpose and nested fan-out is
; preserved. Negative/boundary examples: zero or one configured thread keeps the
; sequential source-order fallback, and effectful branches are not made into
; shared-state parallelism.

!(assertEqualToResult (hyperpose (a b Empty c)) (a b c))
!(assertEqual (collapse (hyperpose (a Empty b))) (a b))
!(assertEqual (collapse (hyperpose ((superpose (x y)) z))) (x y z))

!(pragma! hyperpose-threads 0)
!(assertEqual (select 2 (hyperpose (a b c))) (a b))
!(pragma! hyperpose-threads 1)
!(assertEqual (once (hyperpose (left right))) left)

!(bind! &hyperpose-effects (new-space))
!(pragma! hyperpose-threads 2)
!(collapse
  (hyperpose
    ((add-atom &hyperpose-effects (seen a))
     (add-atom &hyperpose-effects (seen b)))))
!(assertEqual
  (collapse (match &hyperpose-effects (seen $x) $x))
  (a b))
